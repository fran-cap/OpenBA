#pragma once
// ============================================================================
// src/game/Entity/EntityList.h — ENDGAME B.4 W0-H1 (moved from engine/types)
//
// EntityList manages all ComponentEntity instances in the game world.
// It maintains a master array of entity IBPs, 5 update buckets for round-robin
// component ticking, two binary trees (update tree and post-update tree),
// and staging/removal arrays for deferred add/remove operations. No vtable,
// no base. Lives at GameUpdateLoop+0xE8 (m_entityList).
//
// RETAIL (Application-x64.exe):
//   alloc  GameUpdateLoop::Load 0x140096b00: MemAlloc(0xe8, 4)
//   ctor   0x1400c3f90: +0x00/+0x08/+0x10 = 0; the five bucket Arrays at +0x18
//          (vector ctor 0x14003accc); the trees +0x68 / +0x78 as {qword root,
//          dword count}; +0x88 / +0x98 / +0xD0 Arrays = 0; +0xE0 (byte) = 0;
//          +0xA8 = 50, +0xAC = 15, +0xB0 = 8, +0xB4 = 5, +0xB8 = 2 (a qword
//          store: +0xBC = 0), +0xC0 / +0xC8 = 0
//   PreUpdate  0x1400c3bd0 · Update 0x1400c4090 · PostUpdate 0x1400c43d0 ·
//   Serialize 0x1400c44a0 · RemoveEntity 0x1400c38e0 · Shutdown 0x1400c3d30 ·
//   FlagAggregation 0x1400c3470
//
// B.4 W0-H1: the size is 0xE8, not 0xE0: the +0xE0 byte is retail's shutdown
// flag (the tree read and wrote it past the old sizeof).
// B.4 X4-COMP (H5, behaviour): the arrays are Collection::Array members and the
// trees Collection::Set members; the class has retail's ctor and dtor (above),
// and Load constructs it with a placement new.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "system/Value.h"   // System::Time (8 bytes, trivial)
#include "collection/Collection.h"   // Collection::Array, Collection::Set
#include "object/Internal.h"         // Object::InternalPtr (the bucket element)

class ComponentEntity;
namespace Object { template <class T> class InternalPtr; }
namespace IO { class Stream; }

// The element arrays are `Collection::Array<ComponentEntity*>` over retail's
// stride-8 row: every one of them grows through 0x1400032e0 (stride 8, align 8,
// the new tail ZEROED) and is freed by the inlined `~Array` (MemFree, then the
// data qword and the size + capacity qword zeroed: 0x1400c3e83, 0x140001740).
// The row that is exactly that is kSpec_PtrZero (Array<InternalPtr<T>>: the
// element IS an InternalPtr<ComponentEntity> slot, which retail copies without a
// refcount touch). Its definition is src/engine/collection/array.cpp; the
// repeated extern is the Achievement/Goal.h precedent.
namespace rtcoll { extern const ArraySpec kSpec_PtrZero; }   // 0x180008d60

// IBPArray — dynamic array of InternalBasePtr (8-byte elements)
// Layout: +0x00 = data, +0x08 = int32_t size, +0x0C = int32_t capacity
// (Collection::Array's _data / _size / _capacity, asserted in Collection.h).
// The slots are InternalPtr<ComponentEntity> (retail CheckPtr's a slot, then
// reads the bare pointer: 0x1400c38e0), held as ComponentEntity*.
using IBPArray = Collection::Array<ComponentEntity*, Collection::DefaultAllocator,
                                   Collection::ExplicitSpec<&rtcoll::kSpec_PtrZero>>;
static_assert(sizeof(IBPArray) == 0x10, "IBPArray must be 0x10 bytes");

// EntityTree — retail's Set<InternalPtr<ComponentEntity>>: {root qword, count
// dword} (ctor 0x1400c3fe5 / 0x1400c3fe8), nodes of 0x28 bytes inserted by the
// exe's WorkBST core (0x140021a30) and cleared by the inlined Set Clear in the
// dtor (0x1400c3ed6: both subtrees through 0x140014c30, the root's MemFree,
// then root = 0 and the dword count = 0).
using EntityTree = Collection::Set<ComponentEntity*>;
static_assert(sizeof(EntityTree) == 0x10, "EntityTree must be 0x10 bytes");

// EntityTreeNode — the AVL node of EntityTree
// Layout: +0x00 = entity IBP (the key: retail compares it unsigned, 0x1400c38e0),
// +0x08 = parent, +0x10 = left, +0x18 = right, +0x20 = balance, +0x22 = height
using EntityTreeNode = EntityTree::Node;
static_assert(sizeof(EntityTreeNode) == 0x28, "EntityTreeNode must be 0x28 bytes");
static_assert(offsetof(EntityTreeNode, element) == 0x00, "EntityTreeNode::element at +0x00");
static_assert(offsetof(EntityTreeNode, parent) == 0x08, "EntityTreeNode::parent at +0x08");
static_assert(offsetof(EntityTreeNode, left) == 0x10, "EntityTreeNode::left at +0x10");
static_assert(offsetof(EntityTreeNode, right) == 0x18, "EntityTreeNode::right at +0x18");
static_assert(offsetof(EntityTreeNode, balance) == 0x20, "EntityTreeNode::balance at +0x20");
static_assert(offsetof(EntityTreeNode, height) == 0x22, "EntityTreeNode::height at +0x22");
static_assert(offsetof(EntityTreeNode, _padTail) == 0x24, "EntityTreeNode::_padTail at +0x24");

// UpdateBucket — one of 5 round-robin update buckets: a Collection::Array of
// entity IBPs (retail constructs the five with the vector-ctor iterator
// 0x14003accc: element 0x10, count 5, ctor 0x140008080 = the two zero qwords of
// Array's ctor, dtor 0x140001740 = ~Array).
// B.4 H5-P2: the element is retail's InternalPtr<ComponentEntity> (the bucket
// serializer 0x140021ba0 calls InternalBasePtr::Serialize on each slot; Update
// 0x1400c38e0 CheckPtr's a slot, then reads the bare pointer); the row is the
// same kSpec_PtrZero (stride 8, align 8, qword copy, zeroed tail).
using UpdateBucket = Collection::Array<Object::InternalPtr<ComponentEntity>, Collection::DefaultAllocator,
                                       Collection::ExplicitSpec<&rtcoll::kSpec_PtrZero>>;
static_assert(sizeof(UpdateBucket) == 0x10, "UpdateBucket must be 0x10 bytes");

// MasterEntityArray — the +0x08 master array: retail's Array<InternalPtr<ComponentEntity>>.
// The serializer 0x1400c44a0 resizes it through 0x1400032e0 (0x1400c4569) and then, per
// slot, calls InternalBasePtr::Serialize (IAT 0x140152aa0, 0x1400c4587) and nulls a
// slot whose object is not IsDerivedFrom ComponentEntity (cell 0x1401c5d70,
// 0x1400c4595..0x1400c45a5). Same kSpec_PtrZero row as before (ctor zero qwords,
// ~Array in the dtor): only the element's C++ type changes.
using MasterEntityArray = Collection::Array<Object::InternalPtr<ComponentEntity>, Collection::DefaultAllocator,
                                            Collection::ExplicitSpec<&rtcoll::kSpec_PtrZero>>;
static_assert(sizeof(MasterEntityArray) == 0x10, "MasterEntityArray must be 0x10 bytes");

// EntityList — main entity management structure
// Size: 0xE8 bytes (GameUpdateLoop::Load 0x140096b00: MemAlloc(0xe8, 4) + ctor 0x1400c3f90)
struct EntityList {
    System::Time    accumulatedTime;    // +0x00: accumulated simulation time (Serialize 0x1400c44a0:
                                        //        System::Time::Serialize on +0x00)
    MasterEntityArray masterArray;      // +0x08: master array of all entity IBPs {data, count, cap}
    UpdateBucket    buckets[5];         // +0x18: 5 round-robin update buckets (each 0x10)
    EntityTree      updateTree;         // +0x68: the per-tick entity tree {root, int32 count}
                                        //        (ctor 0x1400c3fe8 `MOV dword ptr [RAX+0x8],EDI`)
    EntityTree      postUpdateTree;     // +0x78: the post-update entity tree (ctor 0x1400c3ff7)
    IBPArray        stagingUpdate;      // +0x88: staging array for update tree (added by FlagAggregation)
    IBPArray        stagingPostUpdate;  // +0x98: staging array for post-update tree
    int32_t         processCount[5];    // +0xA8: per-bucket entities to process per tick (int: Serialize
                                        //        0x1400c45d0 writes it through the int-array overload)
    int32_t         cursor[5];          // +0xBC: per-bucket round-robin cursor position (int: 0x1400c45e8)
    IBPArray        removalList;        // +0xD0: entities pending removal
    bool            shuttingDown;       // +0xE0: ctor 0; 0x1400c3d30 (the shutdown drain) sets it
                                        //        first; RegisterEntity drops an entity while it is set
    uint8_t         _padE1[7];          // +0xE1

    // ---- members (retail addresses; bodies in entitylist.cpp) ----
    // 0x1400c3f90. The members' own ctors are retail's stores: the two Arrays'
    // zero qwords, the bucket vector ctor, the Sets' root / count stores.
    EntityList();
    // 0x1400c3e60: Shutdown (0x1400c3d30) is the body; the members are then
    // destroyed in reverse declaration order, which IS retail's sequence:
    // +0xD0, +0x98, +0x88 (~Array), +0x78, +0x68 (Set Clear), the buckets back
    // to front (0x14003ad30 with 0x140001740), +0x08 (~Array).
    ~EntityList();
    EntityList(const EntityList&) = delete;
    EntityList& operator=(const EntityList&) = delete;

    void Update(const System::Time* time);                          // 0x1400c4090 (time: a System::Time*)
    // NOT RETAIL, ext.walk_batch (class A; src/ext/walk_batch/walk_batch.h): Update with the
    // batched snapshot walk, run by GameUpdateLoop::Update on an intermediate sim iteration
    // while the extension is on.
    void UpdateWalkBatch(const System::Time* time);
    // NOT RETAIL, ext.walk_batch_grid (class B; src/ext/walk_batch_grid/walk_batch_grid.h): the same with the batched tick
    // that skips CharacterGrid::Place.
    void UpdateWalkBatchGrid(const System::Time* time);
    void PreUpdate();                                 // 0x1400c3bd0
    void PostUpdateStaging();                         // 0x1400c3ab0
    void PostUpdate();                                // 0x1400c43d0
    void RemoveEntity(ComponentEntity** entityIBP);   // 0x1400c38e0 (an InternalPtr slot)
    void Serialize(IO::Stream& stream);               // 0x1400c44a0
    void FlushForSerialize(IO::Stream& stream);       // Serialize's steps 2-3 (NOT RETAIL as a function)
    void RegisterEntity(Object::InternalPtr<ComponentEntity>* entityIBP, int category);  // 0x1400c48a0 (body in componententity.cpp)
    void QueueDestroy(ComponentEntity* const* entity);           // 0x1400c49d0 (body in nomadcomponent_states.cpp)
    void Shutdown();                                  // 0x1400c3d30 (body in Game/gameupdateloop_teardown.cpp)

private:
    // Update's body; kWalk (a Ext::WalkBatch::Walk) picks the snapshot walk's per-character tick
    // (entitylist.cpp).
    template <int kWalk> void UpdateImpl(const System::Time* time);
    // The retail step-5 tail of Update (0x1400c4090), relocated to the top of the
    // NEXT frame's Update (the tree's one-frame removal deferral; see the body).
    void DrainRemovals();
};
static_assert(offsetof(EntityList, accumulatedTime) == 0x00, "EntityList::accumulatedTime at +0x00");
static_assert(offsetof(EntityList, masterArray) == 0x08, "EntityList::masterArray at +0x08");
static_assert(offsetof(EntityList, buckets) == 0x18, "EntityList::buckets at +0x18");
static_assert(offsetof(EntityList, updateTree) == 0x68, "EntityList::updateTree at +0x68");
static_assert(offsetof(EntityList, postUpdateTree) == 0x78, "EntityList::postUpdateTree at +0x78");
static_assert(offsetof(EntityList, stagingUpdate) == 0x88, "EntityList::stagingUpdate at +0x88");
static_assert(offsetof(EntityList, stagingPostUpdate) == 0x98, "EntityList::stagingPostUpdate at +0x98");
static_assert(offsetof(EntityList, processCount) == 0xA8, "EntityList::processCount at +0xA8");
static_assert(offsetof(EntityList, cursor) == 0xBC, "EntityList::cursor at +0xBC");
static_assert(offsetof(EntityList, removalList) == 0xD0, "EntityList::removalList at +0xD0");
static_assert(offsetof(EntityList, shuttingDown) == 0xE0, "EntityList::shuttingDown at +0xE0");
static_assert(offsetof(EntityList, _padE1) == 0xE1, "EntityList::_padE1 at +0xE1");
static_assert(sizeof(EntityList) == 0xE8, "EntityList is 0xE8 bytes (MemAlloc(0xe8) in 0x140096b00)");

// [PerfX WALKCACHE] (entitylist.cpp, Entity/tick_list.h): mark the entity's slot-8
// tick list stale. Called by ComponentEntity::RefreshEntityFlags, which every
// post-ctor writer of a component's flag low word reaches. No retail counterpart.
void EntityList_TickListInvalidate(const ComponentEntity* entity);
// [PerfX TREE_SNAPSHOT] the walk snapshot is rebuilt at the next walk (the two mutation
// sites in entitylist.cpp; Ext::RenderCulling::BeginFrame when the PERFXTBL side table gains a user).
extern "C" void EntityList_MarkUpdateTreeDirty();
