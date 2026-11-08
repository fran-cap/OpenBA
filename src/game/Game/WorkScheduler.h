#pragma once
// ============================================================================
// src/game/Game/WorkScheduler.h — ENDGAME B.4 GAME-a
//
// The 0x138-byte work scheduler at GameUpdateLoop+0x118 (the member the tree
// calls m_tutorial): every posted Work item by id, the per-profession work
// trees the citizens' job search walks, and the scheduler clock the Work
// slots are handed (+0x130). NOT the Tutorial resource (Tutorial/Tutorial.h).
//
// CLASS NAME UNCONFIRMED: retail gives it no reflected name or vtable;
// "WorkScheduler" is descriptive (Work.h calls it the scheduler).
// No vtable: a plain struct.
//
// RETAIL (Application-x64.exe):
//   ctor 0x1400f9180: +0x00 = 0 (qword), +0x08 = 1, +0x10 = 0, +0x18 = 0,
//     +0x20 = 0, +0x28 = 0; the vector ctor over 15 x 0x10 trees at +0x30
//     (element ctor MainMenuDialog_helper_cab0); +0x120 = 0, +0x128 = 0, +0x130 = 0
//   Serialize 0x1400f9240 (version 3): the 14 / 15 trees at +0x30
//     (TradeDescription_impl_d060), the tree at +0x120 (TradeWork_impl_d5f0), then int +0x00, int
//     +0x04, uint +0x08; on load rebuilds the +0x10 tree (count +0x18) and
//     reads the +0x30 trees by key
//   CitizenAI's work walk 0x1400f7b60 / assign 0x1400f8620 hand the Work
//     slots &+0x130
//   dtor 0x1400f8ff0
//
// The first three dwords: the ctor's qword store at +0x00 zeroes +0x00 and
// +0x04, then +0x08 = 1 (0x1400f9180). The tree's Update increments +0x08 each
// simulation step and adds dt to +0x130; those two are named from that, the
// rest keep typed guess names.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "system/Value.h"   // System::Time (8 bytes, trivial)
#include "Action/PathAvl.h"   // AvlTreeHead (the +0x10 queue-order tree header)
class Work;
class CitizenComponent; class ComponentEntity; class Profession;
struct WorkSchedulerIdNode; struct WorkSchedulerCountNode; struct WorkSchedulerBucketNode;   // Game/WorkSchedulerNodes.h
namespace IO { class Stream; }

// One {root, count} tree header of the +0x30 array (the vector ctor's element,
// 0x10 bytes).
struct WorkSchedulerTree {
    WorkSchedulerIdNode* root;   // +0x00 tree root (nodes: int key at +0x00; Game/WorkSchedulerNodes.h)
    int32_t count;         // +0x08
    uint8_t _pad0C[4];     // +0x0C
};

static_assert(sizeof(WorkSchedulerTree) == 0x10, "WorkSchedulerTree is 0x10 (the vector ctor's stride)");
static_assert(offsetof(WorkSchedulerTree, root) == 0x00, "WorkSchedulerTree::root @ +0x00");
static_assert(offsetof(WorkSchedulerTree, count) == 0x08, "WorkSchedulerTree::count @ +0x08");
static_assert(offsetof(WorkSchedulerTree, _pad0C) == 0x0C, "WorkSchedulerTree +0x0C");

// The node of the {root, count} tree at WorkScheduler +0x20 / +0x28. Retail: the
// ctor 0x1400f9180 zeroes +0x20 / +0x28, Serialize 0x1400f9240 does not touch them,
// the dtor 0x1400f8ff0 (0x1400f90ce..) frees the root's children +0x18 / +0x20
// through the generic link free 0x14000e320 (recurses on +0x18 / +0x20, MemFree, no
// element dtor) and MemFree's the root. No other reader or writer was found (the
// scheduler insert 0x1400f88a0 touches +0x10 / +0x30 / +0x120 only), so only the two
// links the free walks are named; the rest of the node, and its allocation size, are
// unknown. 0x14000e320 is shared by many node types (the id trees' 0x14000dcb0, the
// work walk's candidate tree 0x1400f79b0 ...): it does not identify the element.
struct WorkSchedulerNode20 {
    uint8_t              unreadElementAndParent[0x18];   // +0x00 element / parent: no retail reader found
    WorkSchedulerNode20* left;               // +0x18 freed first by 0x14000e320
    WorkSchedulerNode20* right;              // +0x20
};
static_assert(sizeof(WorkSchedulerNode20) == 0x28, "the links 0x14000e320 walks end at +0x28");
static_assert(offsetof(WorkSchedulerNode20, unreadElementAndParent) == 0x0, "WorkSchedulerNode20::unreadElementAndParent");
static_assert(offsetof(WorkSchedulerNode20, left) == 0x18, "WorkSchedulerNode20::left @ +0x18 (0x14000e320)");
static_assert(offsetof(WorkSchedulerNode20, right) == 0x20, "WorkSchedulerNode20::right @ +0x20 (0x14000e320)");

class WorkScheduler {
public:
    // Better names (CAI-6, from the code; NOT renamed: readers in other blocks' files
    // spell the field_0x.. names; known_port_issues 14f):
    int32_t           lastWorkId;       // +0x00 (Serialize int) the work-id counter (wraps at 0x7ffffffe)
    int32_t           nextQueueOrder;       // +0x04 (Serialize int) the queue-order counter
    uint32_t          stepCount;        // +0x08 ctor 1 (Serialize uint); +1 per simulation step
    uint8_t           _pad0C[4];        // +0x0C
    AvlTreeHead<WorkSchedulerCountNode> queueOrderTree;   // +0x10 the queue-order tree {root (int key; rebuilt on load), count +0x18}
    WorkSchedulerNode20* unusedTreeRoot;    // +0x20 ctor 0; tree root (freed by the dtor only)
    int32_t           unusedTreeCount;       // +0x28 ctor 0; its count
    uint8_t           _pad2C[4];        // +0x2C
    WorkSchedulerTree trees[15];        // +0x30 (TradeDescription_impl_d060: 14 before version 3)
    WorkSchedulerBucketNode* professionBucketRoot; // +0x120 tree root (TradeWork_impl_d5f0) the per-profession bucket tree
    int32_t           professionBucketCount;      // +0x128 tree count (the bucket tree's)
    uint8_t           _pad12C[4];       // +0x12C
    System::Time      time;             // +0x130 the scheduler clock (+= dt per step)

    void Serialize(IO::Stream& stream);   // 0x1400f9240 (goals_serialize.cpp, GAME-b)
    void ValidateAll();                   // 0x1400f8270: slot 9 on every Work of trees[0] (GAME-b)
    Work* FindWorkAtCell(int cell);       // 0x1400f8d50: the trees[0] Work whose target cell (+0x28) is `cell` (B4 PLAYER; Player/keystone_a_removalcalcpath.cpp)
    void  PrioritizeWork(int workId);     // 0x1400f9ad0: re-key the work in the +0x10 tree below its minimum (B4 PLAYER; same file)
    bool TryCompleteTask(CitizenComponent* const* entityRef, int taskIdx, ComponentEntity* const* targetRef, uint32_t taskParam) const;   // 0x1400f8760 (Action/action_base_complete.cpp)
    void TaskReassign(CitizenComponent* const* entityRef, int taskIdx, ComponentEntity* const* targetRef, uint32_t taskParam) const;   // 0x1400f8800 (Action/action_base_complete.cpp)
    void TaskCleanup(int taskIdx);   // 0x1400f8e30 (Action/action_base_complete.cpp)
    int Insert(Object::InternalPtr<Work>* pWorkIBP, uint32_t code, char fromExisting, int prio);   // 0x1400f88a0 (Component/citizenai_work_scheduler.cpp)
    void Rekey(uint32_t workId, Profession* oldKey, Profession* newKey);   // 0x1400f8400 (Component/citizenai_work_scheduler.cpp)
    void Teardown();   // 0x1400f8ff0 (the teardown body; no C++ dtor runs) (Game/gameupdateloop_teardown.cpp)
    uint64_t TryAssignWorkplace(CitizenComponent** citizenRef, int cellId, char flag);   // 0x1400f8620 (Work/work_base_grid.cpp)
};

static_assert(sizeof(WorkScheduler) == 0x138, "WorkScheduler is 0x138 (Load MemAlloc(0x138, 4))");
static_assert(offsetof(WorkScheduler, lastWorkId) == 0x00, "WorkScheduler +0x00");
static_assert(offsetof(WorkScheduler, nextQueueOrder) == 0x04, "WorkScheduler +0x04");
static_assert(offsetof(WorkScheduler, stepCount) == 0x08, "WorkScheduler::stepCount @ +0x08");
static_assert(offsetof(WorkScheduler, _pad0C) == 0x0C, "WorkScheduler +0x0C");
static_assert(offsetof(WorkScheduler, queueOrderTree) == 0x10, "WorkScheduler +0x10");
static_assert(offsetof(WorkScheduler, unusedTreeRoot) == 0x20, "WorkScheduler +0x20");
static_assert(offsetof(WorkScheduler, unusedTreeCount) == 0x28, "WorkScheduler +0x28");
static_assert(offsetof(WorkScheduler, _pad2C) == 0x2C, "WorkScheduler +0x2C");
static_assert(offsetof(WorkScheduler, trees) == 0x30, "WorkScheduler::trees @ +0x30");
static_assert(offsetof(WorkScheduler, professionBucketRoot) == 0x120, "WorkScheduler +0x120");
static_assert(offsetof(WorkScheduler, professionBucketCount) == 0x128, "WorkScheduler +0x128");
static_assert(offsetof(WorkScheduler, _pad12C) == 0x12C, "WorkScheduler +0x12C");
static_assert(offsetof(WorkScheduler, time) == 0x130, "WorkScheduler::time @ +0x130");
