#pragma once
// ============================================================================
// src/game/Work/Work.h — ENDGAME B.3.8 block WRK (B38_RULES.md §2.1, D1-D5)
//
// `Work`, the exe's work-item base (a job a citizen can take: build a road,
// harvest a field, fight a fire …), as a REAL C++ class whose vtable the
// compiler generates. Global namespace, as in retail (reflected name "Work",
// parent literal "Object::Internal", abstract: create = 0x140003730).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §1 / §6):
//   table 0x140161a80, 13 slots, ti 0x1401c6cf8
//   [0]  0x14002ca60 scalar deleting dtor: stamp 0x140161a80, `~Internal`,
//        `if (flags & 1) MemFree`. ~Work has no body.
//   [1]  0x14002ca50 TypeName (`MOV RAX,[ti+8]`)   [2] 0x14002ca40 Type
//   [3]  0x1400f7400 Persist (the Work base Serialize, version 3; ported in
//        work_base_reimpl.cpp; the leaves' Persist bodies call it QUALIFIED)
//   [4]  _purecall                                   (the tree's Execute)
//   [5]  0x1400f7540 `XOR AL,AL; RET` (in 22 tables) (the tree's Slot5)
//   [6]-[11] _purecall                               (OnComplete … GetPriority)
//   [12] 0x140006b10 `RET 0` (in 25 tables)
//   The shared base ctor 0x1400f72e0 (Work_Ctor25) and the default base init
//   every leaf ctor inlines are below.
//
// SLOT NAMES 4-12 have no retail symbol; they are the tree's (the old
// work_base_standalone.h). ARGUMENT LISTS: the union of the parameter lists the
// 21 leaves' C bodies declared (every C body took `self` first, i.e. the member
// ABI), corrected from retail for [4], [9] and [12] (B3X W0 G-B). The retail
// callers read so far: CitizenAI's work walk 0x1400f7b60 and 0x1400f8620
// dispatch [4] as `(work, &citizen, ctx+0x130)` and [9] with RCX only;
// the scheduler dispatches [10] as `(work, retbuf)`. The other slots' callers
// are NOT re-read (B38_RULES §6): a leaf ignores the registers it does not
// read, exactly as its C body did.
//
// LAYOUT: Object::Internal (0x10) + 0x38 = 0x48. The field names are the ones
// the leaves' PODs used (engine/types/BuildActions.h); +0x10 and +0x18 are
// InternalPtrs (Persist post-validates +0x10 as a ComponentEntity and +0x18 as
// a WorkComponent), +0x30/+0x38 are System::Time values (Persist v3+).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "object/Internal.h"
#include "Object/exe_type.h"   // DECLARE_EXE_TYPE (ENDGAME B.3.9 RF)
#include "system/Value.h"      // System::Time (+0x30 / +0x38; trivial, 8 bytes)
#include "collection/Collection.h"   // Collection::Set<T>::Node (the leaves' worker sets)

class ComponentEntity;   // B4 WORK-a: _entity typed (H4)
class WorkComponent;     // B4 WORK-c: _workComponent typed (H4)
class CitizenComponent;
class CompleteTask;
class Profession;        // B4 P2 S2: slot 10's return buffer

// The AVL sets the Work leaves keep their workers in are Collection::Set
// instantiations the exe inlines (insert / erase / serialize: work_base_bst.cpp
// and work_base_cellbst.cpp). Two element shapes:
//   the citizens working the job, keyed on the citizen (0x28-byte node; 0x1400296b0
//   validates each key against CitizenComponent);
using WorkCitizenSetNode = Collection::Set<CitizenComponent*>::Node;
//   the map cell -> citizen of the area works (0x30-byte node, int key; the insert
//   0x140029a40 copies the 0x10-byte element).
struct WorkCellAssignment {
    int32_t           cell;      // +0x00 the key: the index into the work's cell list
    uint32_t          _pad04;    // +0x04 copied with the element, never read
    Object::InternalPtr<CitizenComponent> citizen;   // +0x08
};
using WorkCellSetNode = Collection::Set<WorkCellAssignment>::Node;
//   the entity -> citizen pairs of ClearArea's +0x48 set (0x30-byte node keyed on the
//   entity; the serialize 0x140029c10 validates ComponentEntity, then CitizenComponent).
struct WorkEntityAssignment {
    Object::InternalPtr<ComponentEntity>  entity;    // +0x00 the key
    Object::InternalPtr<CitizenComponent> citizen;   // +0x08
};
using WorkEntitySetNode = Collection::Set<WorkEntityAssignment>::Node;
static_assert(sizeof(WorkCitizenSetNode) == 0x28, "citizen set node: MemAlloc(0x28) in 0x140021b40");
static_assert(sizeof(WorkEntitySetNode) == 0x30, "entity set node: MemAlloc(0x30) in 0x140029b40");
static_assert(sizeof(WorkCellSetNode) == 0x30, "cell set node: MemAlloc(0x30)");

// The per-spot state of a work with fixed positions: one int32 per spot in
// FishingWork::spotStates (+0x58) and ConsumeProduce::_spotStates (+0x68). Both arrays stay
// Collection::Array<int32_t> (the 0x1400021d0 trait row, Persist's int-array overload);
// the enum only names the values retail stores. The cycle: Execute claims an Available
// spot (InUse); OnComplete, or Validate for a spot no citizen holds, marks it NeedsReset;
// Execute returns it to Available once the spot's map cell has no occupant.
enum class WorkSpotState : int32_t {
    Available  = 0,   // free; the create path zero-fills every spot
    InUse      = 1,   // claimed by the citizen sent there
    NeedsReset = 2,   // done or abandoned, held until the map cell is clear
};
static_assert(sizeof(WorkSpotState) == sizeof(int32_t), "WorkSpotState is one spot-array element");


class Work : public Object::Internal {
    DECLARE_EXE_TYPE(Work)

public:
    // ---- the 13 slots, in retail order -------------------------------------
    // No body in retail (slot 0 only re-stamps the table before ~Internal).
    ~Work() override = default;                                      // [0]  0x14002ca60
    // [1]/[2]: the abstract base answers its own record (ENDGAME B.3.9 RF:
    // DEFINE_EXE_TYPE with retail's abstract factory); every leaf overrides both.
    // [1] TypeName 0x14002ca50 and [2] Type 0x14002ca40: DECLARE_EXE_TYPE (retail `MOV RAX,[ti+8]` / `MOV EAX,[ti]`).
    void        Persist(IO::Stream& stream) override;                       // [3]  0x1400f7400
    // [4] retail passes TWO arguments: RDX = &citizen copy, R8 = scheduler
    // +0x130 (0x1400f7bdd/0x1400f7be4 and 0x1400f86c7/0x1400f86ce; R9 is never
    // set, and no body reads it -- BuildRoad 0x1400e4190 overwrites R8 unread).
    // B4 P2 S2: RDX is an InternalPtr<CitizenComponent> copy (`MOV RAX,[RSI];
    // MOV [RSP+0x40],RAX; LEA RDX,[RSP+0x40]`), R8 the scheduler's System::Time
    // clock (WorkScheduler::time +0x130). [5] is dispatched the same way (0x1400f871a).
    virtual int   Execute(CitizenComponent** citizen, System::Time* context) = 0;          // [4]  pure
    virtual int   Slot5(CitizenComponent** citizen, System::Time* context);                // [5]  0x1400f7540 → 0
    // [6]/[7] from 0x1400f8760 / 0x1400f8800 (CompleteTask's task completion): RDX =
    // a copy of the action's InternalPtr<CitizenComponent> (+0x20), R8 = a copy of its
    // InternalPtr<ComponentEntity> target (+0x28), R9D = the task parameter. ClearArea
    // 0x1400e5e30 keys its entity set on [R8]; the OnCancel bodies (0x1400e5e00) copy
    // both and re-dispatch [6].
    virtual int   OnComplete(CitizenComponent** citizen, ComponentEntity** arg2, int arg3) = 0;// [6]  pure
    virtual int   OnCancel(CitizenComponent** citizen, ComponentEntity** arg2, int arg3) = 0;  // [7]  pure
    virtual int   Cleanup() = 0;                                      // [8]  pure
    // [9] retail passes `this` only: 0x1400f7bc3, 0x1400f86ad, 0x1400f86f4,
    // 0x1400f82b1, 0x1400f99a4 set RCX and nothing else. Bodies (BuildRoad
    // 0x1400e40c0, Pickup 0x1400f2e20) forward the stale RDX to
    // Work::ValidateTask 0x1400f7100, which spills it to its home slot and
    // overwrites it unread (0x1400f71a4) -- ClearArea 0x1400e5b10 never sets it.
    virtual int   Validate() = 0;                                     // [9]  pure
    // [10] the "who can do this" key (a u64 written through the hidden return
    // buffer, returned as the buffer): retail RCX = this, RDX = retbuf. The key is
    // the InternalPtr<Profession> of who can do the job: ForesterWork 0x1400eb910
    // stores WorkPlaceComponent +0x48 into [RDX] and returns RDX.
    virtual Profession** GetTarget(Profession** retbuf) = 0;                        // [10] pure
    virtual int   GetPriority() = 0;                                  // [11] pure
    // [12] retail passes EDX = the serializer version (Goals_Serialize
    // 0x1400f9335..0x1400f9340: `CALL GetVersion; MOV EDX,EAX; MOV RCX,RDI;
    // CALL [RSI+0x60]`). Every body in the tree is an empty `RET 0`.
    virtual void  Slot12(int version);                                // [12] 0x140006b10 RET 0

    // ---- the 0x38 bytes Work adds, retail order ------------------------------
    // PUBLIC while unconverted bodies read them at raw offsets (decision 1).
    Object::InternalPtr<ComponentEntity> _entity;   // +0x10 the owning entity (Persist 0x1400f7400 validates ComponentEntity)
    Object::InternalPtr<WorkComponent>   _workComponent;   // +0x18 the entity's WorkComponent (Persist validates it)
    int32_t  _workId;   // +0x20 work id (-1)
    int32_t  _queueOrder;   // +0x24 (Persist copies +0x20 into it before v1)
    int32_t  _cell;   // +0x28 target map cell (-1)
    int32_t  _lastRejectStep;   // +0x2c
    System::Time _waitTime;   // +0x30 System::Time (Persist v3, ?Serialize@Time@System@@)
    System::Time _waitBaseTime;   // +0x38 System::Time (Persist v3)
    int32_t  _tooFarCount;   // +0x40
    uint8_t  _pad44[4];  // +0x44 never written

    // The body of the shared base ctor 0x1400f72e0 MINUS its `??0Internal` call
    // and vptr store. `Work(void*)` runs it after the compiler's stamps (its
    // by-address door `Work_Ctor25_Impl` went at ENDGAME B.3.9 OBJ).
    void InitFromEntity(ComponentEntity* const* pEntityIBP);

    // 0x1400f7100: is `citizen` still working this job? True when one of the
    // citizen's actions up to the running one (CharacterComponent +0x48 array,
    // +0x60 index) is a CompleteTask whose task index is this work's id (+0x20);
    // that task is written to `out` when `out` is non-null. RDX is spilled and
    // never read (0x1400f7100 stores it, 0x1400f71a4 overwrites the slot).
    bool ValidateTask(void* unusedRdx, CitizenComponent** citizen, CompleteTask** out);   // LEFT: void* is a dead argument (retail 0x1400f7100 spills RDX and never reads it; all 21 callers pass nullptr), not an object

    // 0x1400e3db0: the auto-pickup check the gatherer leaves run first in Execute:
    // when the entity's AutoPickupComponent is enabled, offer the citizen each work id
    // it lists (the scheduler's TryAssignWorkplace 0x1400f8620), dropping ids the
    // scheduler no longer knows. Returns 2 when one took the citizen, else 0.
    int TryAutoPickup(CitizenComponent** citizenRef);

    // 0x1400f6ef0: the build-progress / time-budget step (reads and updates +0x30,
    // +0x38 and +0x40). The low byte of the result is "keep going"; the tree's callers
    // read the whole register, so the upper bytes are kept as retail leaves them.
    uint64_t StepProgress(CitizenComponent** citizen, uint32_t cellA, uint32_t cellB, const System::Time* now);

protected:
    // The default base init every retail leaf ctor inlines after `??0Internal`
    // and the 0x140161a80 stamp: +0x10/+0x18 = 0, +0x20 = -1, +0x24 = 0,
    // +0x28 = -1, +0x2c = 0, +0x30/+0x38 = 0, +0x40 = 0 (e.g. BuildRoad_Ctor
    // 0x1400e4ad0). The vptr stores are the compiler's.
    Work();
    // 0x1400f72e0: the entity-seeded base ctor (Work_Ctor25).
    explicit Work(ComponentEntity* const* pEntityIBP);

public:
    // Vtable() (DECLARE_EXE_TYPE) is the compiler's vtable (B38 D5): served at
    // 0x140161a80 while by-address readers (the unconverted dtors that re-stamp
    // the base) remain.
};

static_assert(sizeof(Work) == 0x48, "Work is 0x48 bytes (0x10 Internal + 0x38)");
static_assert(offsetof(Work, _pad44) == 0x44, "Work::_pad44");
static_assert(alignof(Work) == 8, "Work is 8-aligned");
static_assert(offsetof(Work, _entity) == 0x10, "Work +0x10 entity InternalPtr (Persist 0x1400f7400)");
static_assert(offsetof(Work, _workComponent) == 0x18, "Work +0x18 WorkComponent InternalPtr");
static_assert(offsetof(Work, _workId) == 0x20, "Work +0x20 work id, -1 (0x1400f72e0)");
static_assert(offsetof(Work, _queueOrder) == 0x24, "Work +0x24");
static_assert(offsetof(Work, _cell) == 0x28, "Work +0x28 target cell, -1 (0x1400f72e0)");
static_assert(offsetof(Work, _lastRejectStep) == 0x2c, "Work +0x2c");
static_assert(offsetof(Work, _waitTime) == 0x30, "Work +0x30 System::Time (Persist v3)");
static_assert(offsetof(Work, _waitBaseTime) == 0x38, "Work +0x38 System::Time (Persist v3)");
static_assert(offsetof(Work, _tooFarCount) == 0x40, "Work +0x40 (Persist v3)");

