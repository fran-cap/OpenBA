#pragma once
// ============================================================================
// src/game/Entity/CitizenBehavior.h — ENDGAME B.4 W0-H1
//
// `CitizenBehavior` (0x1C0): the town's population manager — the lists of
// citizens and of the buildings they use, the per-profession resource counts,
// the stats history and the path-distance graph. No vtable, no base: a plain
// class. Lives at GameUpdateLoop+0xF0 (m_citizenList). Profile source path:
// Entity\CitizenBehavior.cpp.
//
// RETAIL (Application-x64.exe):
//   alloc  GameUpdateLoop::Load 0x140096b00: MemAlloc(0x1c0, 4) + ctor
//   ctor   0x140090a30: zeroes +0x00..+0x1B3 (the two trees +0x100 / +0x160 as
//          {qword root, dword count}); Array SetSize +0x170 to 21 (0x1400177c0)
//          with the per-entry defaults, +0x180 to 2 (0x14000f090), +0x150 to 71
//          (0x140017640); +0x1A8 = 0; +0x1B8 = MemAlloc(0x28, 4) PathGraph
//          {0, 0, -1, {0, 0}}
//   Serialize 0x14008f760 (version 6): each IBP array's count, then its
//          elements through InternalBasePtr::Serialize (0x140152aa0) + an
//          IsDerivedFrom against the cell named on the field; +0x110 / +0x120
//          through the ExternalPtr array body 0x1400049c0; +0x190 / +0x198 /
//          +0x1A0 Time (0x140152aa8); +0x1A8 float (0x140152a60); +0x1AC /
//          +0x1B0 int (0x140152ac8)
//   Update 0x14008c2b0: +0x190 += dt, +0x198 -= dt, +0x1A0 -= dt; +0x1B8 the
//          path graph; +0x198 < 1 -> 0x14008a730; +0x1A0 < 0 -> +0x1A8 = 0
//
// B.4 X5-COMP (H5, behaviour): the arrays are Collection::Array members and the
// two bucket trees Collection::Set members, typed together so that the members'
// reverse-declaration-order destruction is retail's dtor 0x1400907b0 (which frees
// the arrays INTERLEAVED with the trees); the class has retail's ctor and dtor
// and Load constructs it with a placement new. Element types are the
// serializer's validated cells; an InternalPtr<T> element is an InternalPtr<T>
// (B4 S4 IBP), an ExternalPtr<T> element the bare `T*`.
//
// The engine/types/CitizenBehavior.h header forwards here and still carries the
// `CitizenBehaviorRaw` accessor mirror its bodies use (H10: it goes with its last
// user).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "collection/Collection.h"   // Collection::Array<int32_t> (the +0x140 buckets)
#include "system/Value.h"            // System::Time (the five clocks)
#include "Action/PathAvl.h"          // AvlNode30 / AvlTreeHead (the PathGraph distance cache)
#include "object/Internal.h"         // Object::InternalPtr<T> (the +0x00..+0xF0 list elements)

class ComponentEntity;
class CemeteryComponent;
class HappinessComponent;
class AttendanceComponent;
class HealerComponent;
class LivestockComponent;
class PastureComponent;
class CitizenComponent;
class HerdComponent;
class PopulationComponent;
class BuildComponent;
class WellComponent;
class DiseaseComponent;
class ComponentDescription;
class Profession;
class Map;
namespace IO { class Stream; }

// ============================================================
// ProfessionResourceEntry — one element of the +0x170 array (stride 0x20).
// ============================================================
// The ctor sets +0x08 of the 21 entries (200, 200, 200, 200, 50, 5000, 100, 50,
// 500, 200, 200, then 1000 x10). The serializer writes five ints (+0x00, +0x04,
// +0x08, +0x0C, +0x10) and the Time at +0x18; +0x14 is not serialized.
struct ProfessionResourceEntry {
    int32_t  currentCount;    // +0x00
    int32_t  previousCount;   // +0x04
    int32_t  maxCount;        // +0x08: set in ctor (200, 50, 5000, etc.)
    int32_t  consumedCount;      // +0x0C: int, serialized
    int32_t  producedCount;        // +0x10
    int32_t  _pad14;        // +0x14: not serialized
    System::Time timer;       // +0x18: System::Time
};
static_assert(sizeof(ProfessionResourceEntry) == 0x20, "ProfessionResourceEntry size");
static_assert(offsetof(ProfessionResourceEntry, currentCount)  == 0x00, "");
static_assert(offsetof(ProfessionResourceEntry, previousCount) == 0x04, "");
static_assert(offsetof(ProfessionResourceEntry, maxCount)      == 0x08, "");
static_assert(offsetof(ProfessionResourceEntry, consumedCount)    == 0x0C, "");
static_assert(offsetof(ProfessionResourceEntry, producedCount)      == 0x10, "");
static_assert(offsetof(ProfessionResourceEntry, _pad14)      == 0x14, "");
static_assert(offsetof(ProfessionResourceEntry, timer)         == 0x18, "");

// ============================================================
// ResourceNotifyEntry — one element of the +0x180 array (stride 0x10, 2 of them).
// ============================================================
// Serialized as int, int, Time.
struct ResourceNotifyEntry {
    int32_t  current;         // +0x00: current count
    int32_t  maxCapacity;     // +0x04: max capacity (5% threshold = max/20)
    System::Time timer;       // +0x08: System::Time, countdown to the next notification
};
static_assert(sizeof(ResourceNotifyEntry) == 0x10, "ResourceNotifyEntry size");
static_assert(offsetof(ResourceNotifyEntry, current)     == 0x00, "");
static_assert(offsetof(ResourceNotifyEntry, maxCapacity) == 0x04, "");
static_assert(offsetof(ResourceNotifyEntry, timer)       == 0x08, "");

// ============================================================
// PopulationSummary — a 0x18-byte stats record (tree-side; not a retail field
// of CitizenBehavior).
// ============================================================
struct PopulationSummary {
    int32_t  totalWorkers;     // +0x00: total workers (alive + not dead)
    int32_t  totalWorkersIncl; // +0x04: total including dead
    int32_t  adultCount;         // +0x08
    int32_t  childCount;         // +0x0C
    int32_t  deadCount;        // +0x10: dead citizens this frame
    int32_t  educatedCount;    // +0x14: educated workers count
};
static_assert(sizeof(PopulationSummary) == 0x18, "PopulationSummary size");
static_assert(offsetof(PopulationSummary, totalWorkers)     == 0x00, "");
static_assert(offsetof(PopulationSummary, totalWorkersIncl) == 0x04, "");
static_assert(offsetof(PopulationSummary, adultCount)         == 0x08, "");
static_assert(offsetof(PopulationSummary, childCount)         == 0x0C, "");
static_assert(offsetof(PopulationSummary, deadCount)        == 0x10, "");
static_assert(offsetof(PopulationSummary, educatedCount)    == 0x14, "");

// ============================================================
// PathCellPair — one element of PathGraph::m_pairs (stride 8, align 4)
// ============================================================
// BuildPathGraph 0x1400d70c0 appends {max, min} of (entity cell, building cell) with
// MemAlloc(cap * 8, 4); ProcessPaths 0x1400d6f00 reads the two ints as grid cells.
struct PathCellPair {
    int32_t  cellA;           // +0x00: the larger of the two anchor cells
    int32_t  cellB;           // +0x04: the smaller
};
static_assert(sizeof(PathCellPair) == 0x08, "PathCellPair size");
static_assert(alignof(PathCellPair) == 4, "PathCellPair align (MemAlloc(.., 4))");
static_assert(offsetof(PathCellPair, cellA) == 0x00, "");
static_assert(offsetof(PathCellPair, cellB) == 0x04, "");

// The pair BuildPathGraph appends for one (profession entity, residence) cell pair: the
// larger cell first. 0x1400d73d3 / 0x1400d73d7 store {entityCell, buildingCell}, then
// 0x1400d73de CMP R11D,R10D / JGE and 0x1400d73e3 / 0x1400d73e7 swap them.
inline PathCellPair MakePathCellPair(int32_t entityCell, int32_t buildingCell) {
    if (entityCell < buildingCell) {
        return PathCellPair{ buildingCell, entityCell };
    }
    return PathCellPair{ entityCell, buildingCell };
}

// CollectStats' educated sample: educated adults as a percentage of the adults
// (0x14008a98b DIVSS, 0x14008a9ba MULSS by 100.0f, 0x14008a9c5 CVTTSS2SI).
inline int32_t EducatedPercent(int32_t educated, int32_t adults) {
    if (adults > 0) {
        return static_cast<int32_t>((static_cast<float>(educated) / static_cast<float>(adults)) * 100.0f);
    }
    // NOT RETAIL: the zero test. With no adult, educated is 0 too and retail divides 0 by 0
    // unguarded; CVTTSS2SI of the NaN stores the integer indefinite 0x80000000. The C++
    // conversion of a NaN is undefined behaviour, so the value is written explicitly.
    return INT32_MIN;
}

// ============================================================
// PathCostNode — a node of the PathGraph distance cache (m_bstRoot)
// ============================================================
// Insert 0x140025a10: the root is MemAlloc(0x30, 8) with dwords [0..2] = the key
// and value, [4..10] = 0; a child is built by 0x14001cc40: MemAlloc(0x30, 8),
// dwords [0..2], qword +0x10 = the parent, qwords +0x18 / +0x20 = 0, dword +0x28 = 0.
// The key compares signed ints, high first (key0 = max(region A, region B), then
// key1 = the min); key2 is the path cost. The rebalance is 0x14000e360. The +0x0C
// dword is never written. The node is Action/PathAvl.h's AvlNode30 (the same 0x30
// layout the region-pair cache's tree uses; AvlInsert30 0x14001c940 inserts it).
using PathCostNode = AvlNode30;
static_assert(sizeof(PathCostNode) == 0x30, "PathCostNode is MemAlloc(0x30, 8) (0x140025a10, 0x14001cc40)");

// ============================================================
// The +0x100 (Profession) / +0x160 (ComponentDescription template) entity-bucket
// trees: Collection::Set<CitizenBucket<Key>> (B.4 X5-COMP, H5)
// ============================================================
// Node ctor 0x140017460: MemAlloc(0x38, 8); +0x00 = the key (qword), +0x08 /
// +0x10 = 0 then the element Array copied in by 0x140015ec0, +0x18 = the parent,
// +0x20 / +0x28 = 0, dword +0x30 = 0 (the int16 balance and height). The retrace
// 0x140014b40 reads +0x30 / +0x32 as two int16 (MOVSX). Find / insert 0x1400172f0
// compares the key as an UNSIGNED qword (the key is the object's address), +0x20
// left, +0x28 right. That is Collection::Set's node over a 0x18-byte element
// {Key* key; Array<InternalPtr<ComponentEntity>> entities} (parent +0x18, left
// +0x20, right +0x28, balance +0x30, height +0x32, _padTail +0x34). The trees'
// Clear 0x1400173d0 (both subtrees through 0x1400174e0, then the root's entity
// Array freed and zeroed, the root freed, root = 0, dword count = 0) is Set's
// Clear with the element's ~Array; it runs from ~CitizenBehavior. The nodes are
// still built by the tree's retail-shaped raw allocations (by field name).
namespace rtcoll { extern const ArraySpec kSpec_PtrZero; }   // 0x180008d60 (src/engine/collection/array.cpp)

// Every pointer list of this class (and each bucket's entity list) grows through
// retail's stride-8 row (0x1400032e0 / 0x140001bb0 / 0x140003110: stride 8,
// align 8, the new tail ZEROED, no element destroy) and is freed by the inlined
// ~Array (MemFree, then the data qword and the size + capacity qword zeroed:
// 0x1400907e9 ...). That row is kSpec_PtrZero (the row the tree's AsArray<Ptr8>
// bridge reached; Entity/EntityList.h's IBPArray is the same instantiation).
// An ExternalPtr<T> element (+0x110 / +0x120) is held as the bare T*; an
// InternalPtr<T> element is the InternalPtr (CitizenIbpList below). Retail's dtor
// runs no element destroy over either (0x1400907b0).
template <class T>
using CitizenPtrList = Collection::Array<T*, Collection::DefaultAllocator,
                                         Collection::ExplicitSpec<&rtcoll::kSpec_PtrZero>>;
// The InternalPtr lists (the sixteen +0x00..+0xF0 members and each bucket's entity
// list): retail's Array<InternalPtr<T>>, whose element the serializer hands to
// InternalBasePtr::Serialize (0x140152aa0, 0x14008f7f8) by address. Same row
// (kSpec_PtrZero), same 8-byte element; the spec runs no element ctor / dtor, so
// the class's ctor 0x140090a30 and dtor 0x1400907b0 are unchanged by the element type.
template <class T>
using CitizenIbpList = Collection::Array<Object::InternalPtr<T>, Collection::DefaultAllocator,
                                         Collection::ExplicitSpec<&rtcoll::kSpec_PtrZero>>;

template <class Key>
struct CitizenBucket {
    Key*                            key;       // +0x00 compared by address (unsigned)
    CitizenIbpList<ComponentEntity> entities;  // +0x08 Array<InternalPtr<ComponentEntity>>
};
template <class Key>
using CitizenBucketSet  = Collection::Set<CitizenBucket<Key>, Collection::AVLCompare,
                                          Collection::DefaultAllocator, Key*>;
template <class Key>
using CitizenBucketNode = typename CitizenBucketSet<Key>::Node;

using ProfessionBucket = CitizenBucketNode<Profession>;            // +0x100
using TemplateBucket   = CitizenBucketNode<ComponentDescription>;  // +0x160
static_assert(sizeof(CitizenBucket<Profession>) == 0x18, "bucket element {key, Array}");
static_assert(sizeof(ProfessionBucket) == 0x38, "bucket node is MemAlloc(0x38, 8) (0x140017460)");
static_assert(offsetof(ProfessionBucket, element)  == 0x00, "");
static_assert(offsetof(CitizenBucket<Profession>, key)      == 0x00, "");
static_assert(offsetof(CitizenBucket<Profession>, entities) == 0x08, "");
static_assert(offsetof(ProfessionBucket, parent)   == 0x18, "");
static_assert(offsetof(ProfessionBucket, left)     == 0x20, "");
static_assert(offsetof(ProfessionBucket, right)    == 0x28, "");
static_assert(offsetof(ProfessionBucket, balance)  == 0x30, "");
static_assert(offsetof(ProfessionBucket, height)   == 0x32, "");
static_assert(offsetof(ProfessionBucket, _padTail) == 0x34, "");
static_assert(sizeof(TemplateBucket) == 0x38, "");
static_assert(sizeof(CitizenBucketSet<Profession>) == 0x10, "{root qword, count dword} + pad");

// ---- 0x140017230: the one keyed insert both bucket trees share ---------------
// Retail compiled the bucket Set's keyed insert once and folded the two Key
// instantiations (ICF): 0x140017230 builds a stack element {key, a copy of the
// entity array} (0x140015ec0), inserts it through 0x1400172f0 (the node ctor
// 0x140017460 copies the array again), frees the temporary's buffer and returns
// param_2. The core (Component/keystone_a_restree.cpp) walks the tree through its
// own {root, count} / 0x38-node view; InsertCitizenBucket<Key> is the typed wrapper
// the call sites use.
//
// The array record (param_4): both retail callers pass an empty
// Array<InternalPtr<ComponentEntity>> temporary, 0x10 bytes zeroed as two qwords
// (0x140088e89 / 0x140088e8e). It is a constructor-less POD so a caller's local
// adds no ctor / dtor.
struct CitizenBucketEntities {
    ComponentEntity** data;       // +0x00
    int32_t           count;      // +0x08
    int32_t           capacity;   // +0x0C
};
static_assert(sizeof(CitizenBucketEntities) == 0x10, "the array record 0x140015ec0 copies");
static_assert(offsetof(CitizenBucketEntities, data) == 0x0, "CitizenBucketEntities::data");
static_assert(offsetof(CitizenBucketEntities, count)    == 0x08, "");
static_assert(offsetof(CitizenBucketEntities, capacity) == 0x0C, "");

namespace CitizenBucketCore { struct Tree; struct Node; }   // the core's own view (keystone_a_restree.cpp)
extern "C" CitizenBucketCore::Node** ResidenceComponent_impl_7230(CitizenBucketCore::Tree* tree,
                                                                  CitizenBucketCore::Node** out,
                                                                  const uint64_t* key,
                                                                  const CitizenBucketEntities* entities);  // 0x140017230

template <class Key>
CitizenBucketNode<Key>** InsertCitizenBucket(CitizenBucketSet<Key>* set, CitizenBucketNode<Key>** out,
                                             const Key* const* key, const CitizenBucketEntities* entities) {
    ResidenceComponent_impl_7230(reinterpret_cast<CitizenBucketCore::Tree*>(set),  // CAST-OK: elem_stride
                                 reinterpret_cast<CitizenBucketCore::Node**>(out),  // CAST-OK: elem_stride
                                 reinterpret_cast<const uint64_t*>(key),           // CAST-OK: elem_stride
                                 entities);
    return out;
}

// ============================================================
// WaterCellPair — one element of the +0x130 array (stride 8, align 4; 0x140017f60)
// ============================================================
// Serialized as two ints; FindWater 0x1400881d0 reads the pair's two cell ids,
// PostLoad 0x14008f580 the first (`lVar11 += 8`, 0x14008f5c8).
struct WaterCellPair {
    int32_t  cellA;           // +0x00
    int32_t  cellB;           // +0x04
};
static_assert(sizeof(WaterCellPair) == 0x08, "WaterCellPair size");
static_assert(alignof(WaterCellPair) == 4, "WaterCellPair align (0x140017f60: MemAlloc(.., 4))");
static_assert(offsetof(WaterCellPair, cellA) == 0x00, "");
static_assert(offsetof(WaterCellPair, cellB) == 0x04, "");

// The hand-derived rows of the other four arrays (their growth bodies are retail's
// own monomorphisations, not the trait-derived row). Definitions: Entity/
// citizenbehavior.cpp (the first three), Component/storagecomponent.cpp (the last).
namespace CitizenBehaviorRows {
extern const rtcoll::ArraySpec kSpec_IntAssocOuter;   // 0x140017640 Array<Array<int32>> (+0x140, +0x150)
extern const rtcoll::ArraySpec kSpec_ProfResEntry;    // 0x1400177c0 Array<ProfessionResourceEntry> (+0x170)
extern const rtcoll::ArraySpec kSpec_IntPair;         // 0x140017f60 Array<WaterCellPair> (+0x130)
extern const rtcoll::ArraySpec kSpec_Pod16;           // 0x14000f090 Array<{u64,u64}> (+0x180)
}
using CitizenIntAssocArray = Collection::Array<Collection::Array<int32_t>, Collection::DefaultAllocator,
                                               Collection::ExplicitSpec<&CitizenBehaviorRows::kSpec_IntAssocOuter>>;
using ProfessionResourceArray = Collection::Array<ProfessionResourceEntry, Collection::DefaultAllocator,
                                                  Collection::ExplicitSpec<&CitizenBehaviorRows::kSpec_ProfResEntry>>;
using WaterCellPairArray = Collection::Array<WaterCellPair, Collection::DefaultAllocator,
                                             Collection::ExplicitSpec<&CitizenBehaviorRows::kSpec_IntPair>>;
using ResourceNotifyArray = Collection::Array<ResourceNotifyEntry, Collection::DefaultAllocator,
                                              Collection::ExplicitSpec<&CitizenBehaviorRows::kSpec_Pod16>>;

// ============================================================
// PathGraph — the path-distance sub-object at +0x1B8 (MemAlloc(0x28, 4) in the ctor)
// ============================================================
// ctor 0x140090ce9: +0x00 = 0, +0x08 = 0 (qword: count and capacity), +0x10 = -1,
// +0x18 = 0 (qword), +0x20 = 0 (dword). Update 0x14008c2b0 reads +0x10 as int.
struct PathGraph {
    PathCellPair* m_pairs;    // +0x00: Array data: {int32, int32} grid cell pairs
    int32_t  m_pairCount;     // +0x08: Array count
    int32_t  m_pairCapacity;  // +0x0C: Array capacity
    int32_t  m_pendingIdx;    // +0x10: current processing index (-1 = done, >= 0 = in progress)
    int32_t  _pad14;          // +0x14
    AvlTreeHead<PathCostNode> m_bst;   // +0x18: the distance cache {(high,low) -> distance}: root, count

    // ---- members (B4 WORK-a; no retail symbol, the tree's names; bodies in
    // citizenbehavior.cpp) ----
    bool ProcessPaths();      // 0x1400d6f00 up to 7 pending pair costs per call
    void BuildPathGraph();    // 0x1400d70c0 rebuild the pair list when idle
    // ProcessPaths' tail hands the PathGraph in RCX (0x1400d709f `MOV RCX,RBX`); neither body
    // reads it (they read the GameUpdateLoop singleton). Bodies in Component/citizen_*.cpp.
    void ReassignWorkers();   // 0x1400d62a0
    void AssignCitizens();    // 0x1400d43b0
    // 0x1400d6060 -- AssignCitizens' cost query (fromCell -> toCell), answered from m_bst,
    // the region-pair costs ProcessPaths filled. Retail fills a 0x18 record {citizen,
    // fromCell, toCell, cost}; the first three are the caller's arguments, so the port
    // returns the cost. A region pair that is not in the tree is computed and stored, and
    // that first query returns 0 (docs/retail_bugs.md). Body in citizen_assign_citizens.cpp.
    int RegionPairCost(Map* map, int fromCell, int toCell);

    // NOT RETAIL (HITCH_PAIRSPREAD, Entity/path_cost_index.h): the m_bst map's operations,
    // on the flat index; at COMPPROF=4 also on the retail tree, every lookup compared.
    int32_t* CostFind(int high, int low);              // the cost slot, or nullptr
    void     CostSet(int high, int low, int32_t cost); // insert-or-find, then store (ProcessPaths)
    void     CostAdd(int high, int low, int32_t cost); // insert-or-find, a present cost kept
    void     CostClear();                              // every pair gone (and m_bst freed)
};
static_assert(sizeof(PathGraph) == 0x28, "PathGraph size");
static_assert(offsetof(PathGraph, m_pairs)        == 0x00, "");
static_assert(offsetof(PathGraph, m_pairCount)    == 0x08, "");
static_assert(offsetof(PathGraph, m_pairCapacity) == 0x0C, "");
static_assert(offsetof(PathGraph, m_pendingIdx)   == 0x10, "");
static_assert(offsetof(PathGraph, _pad14)         == 0x14, "");
static_assert(offsetof(PathGraph, m_bst)          == 0x18, "");
// 0x1400171b0 — the PathGraph's members (the +0x18 distance tree, then the +0x00
// pair array); ~CitizenBehavior runs it before freeing the block (body in
// Game/gameupdateloop_teardown.cpp).
extern "C" void ArrayPlusTree_Clear_171b0(PathGraph* self);

// ============================================================
// CitizenBehavior — 0x1C0 bytes
// ============================================================
class CitizenBehavior {
public:
    // +0x00..+0xFF: sixteen Collection::Array<InternalPtr<T>> (0x10 each), T = the
    // cell the serializer validates each element against.
    CitizenIbpList<ComponentEntity>     storageLocations;           // +0x00 (cell 0x1401c5d70 ComponentEntity)
    CitizenIbpList<ComponentEntity>     marketLocations;           // +0x10 (cell 0x1401c5d70)
    CitizenIbpList<ComponentEntity>     residences;           // +0x20 (cell 0x1401c5d70)
    CitizenIbpList<ComponentEntity>     tradePosts;           // +0x30 (cell 0x1401c5d70)
    CitizenIbpList<CemeteryComponent>   cemeteries;           // +0x40 (cell 0x1401c4090)
    CitizenIbpList<HappinessComponent>  happinessComponents;  // +0x50 (cell 0x1401c42c0)
    CitizenIbpList<AttendanceComponent> attendanceComponents; // +0x60 (cell 0x1401c3f40)
    CitizenIbpList<HealerComponent>     healers;              // +0x70 (cell 0x1401c4330)
    CitizenIbpList<LivestockComponent>  livestock;            // +0x80 (cell 0x1401c7970)
    CitizenIbpList<PastureComponent>    pastures;             // +0x90 (cell 0x1401c7ba0)
    CitizenIbpList<CitizenComponent>    citizens;             // +0xA0 (cell 0x1401c7158)
    CitizenIbpList<HerdComponent>       herds;                // +0xB0 (cell 0x1401c7820)
    CitizenIbpList<PopulationComponent> populations;          // +0xC0 (cell 0x1401c4790)
    CitizenIbpList<BuildComponent>      buildComponents;      // +0xD0 (cell 0x1401c7008)
    CitizenIbpList<WellComponent>       wells;                // +0xE0 (cell 0x1401c4c28)
    CitizenIbpList<DiseaseComponent>    diseases;             // +0xF0 (cell 0x1401c4170; serialized at version > 0)

    CitizenBucketSet<Profession> workplacesByProfession;      // +0x100 tree {root, dword count} (0x38-byte nodes; the
                                                   //        key is an ExternalPtr<Profession>, serialized by
                                                   //        0x140018020)
    CitizenPtrList<ComponentDescription> knownRawMaterials; // +0x110 Array<ExternalPtr<ComponentDescription>>
    CitizenPtrList<ComponentDescription> availableFarmTypes; // +0x120 Array<ExternalPtr<ComponentDescription>>
    WaterCellPairArray   waterAccessPairs;              // +0x130 Array of {int32, int32} pairs (0x140017f60)
    CitizenIntAssocArray coarseWaterIndex;              // +0x140 Array of 0x10-byte elements, each an inner int Array
                                                   //        (0x140017640; version > 4): the COARSE water
                                                   //        index, (W>>3)^2 buckets of water ids (< 0: a
                                                   //        well, -1-id into +0xE0; >= 0: a +0x130 pair)
    CitizenIntAssocArray statHistory;              // +0x150 Array of 0x10-byte elements, each an inner int Array;
                                                   //        the ctor sizes it to 71 (0x140017640)
    CitizenBucketSet<ComponentDescription> entitiesByTemplate; // +0x160 tree {root, dword count} (serialized by
                                                   //        0x140017cd0; key a ComponentDescription template)
    ProfessionResourceArray professionResources;   // +0x170 the ctor sizes it to 21
    ResourceNotifyArray  notifyEntries;            // +0x180 the ctor sizes it to 2
    System::Time pathGraphTimer;              // +0x190 System::Time: += dt; drives the path graph (Update)
    System::Time statsTimer;              // +0x198 System::Time: -= dt; < 1 -> 0x14008a730 (Update)
    System::Time diseaseRiskTimer;              // +0x1A0 System::Time: -= dt; < 0 -> +0x1A8 = 0 (Update)
    float    diseaseRisk;                  // +0x1A8 float (serialized by the float overload; Update
                                           //        stores dword 0)
    int32_t  totalTradeValue;                  // +0x1AC int (version > 1)
    int32_t  totalNomadsAccepted;                  // +0x1B0 int (version > 2)
    uint8_t  _pad1B4[4];                   // +0x1B4 (no store in the ctor)
    PathGraph* pathGraph;                  // +0x1B8 MemAlloc(0x28, 4) in the ctor

    // ---- ctor / dtor (B4 X5-COMP, H5; bodies in citizenbehavior.cpp) ----
    // 0x140090a30: the members' own ctors are retail's zero stores (each Array's
    // data + size/capacity qwords, each Set's root qword + count dword), then the
    // body: the three clocks and +0x1AC / +0x1B0 zeroed, +0x170 sized to 21 and the
    // maxCount defaults, +0x180 sized to 2, +0x150 sized to 71, +0x1A8 = 0, the
    // PathGraph allocated.
    CitizenBehavior();
    // 0x1400907b0: the body frees the PathGraph (0x1400171b0, then MemFree); the
    // members are then destroyed in reverse declaration order, which IS retail's
    // sequence: +0x180, +0x170 (~Array), +0x160 (Set Clear 0x1400173d0), +0x150,
    // +0x140 (0x1400175d0: each inner Array, then the outer), +0x130, +0x120,
    // +0x110 (~Array), +0x100 (0x1400173d0), +0xF0 down to +0x00 (~Array).
    ~CitizenBehavior();
    CitizenBehavior(const CitizenBehavior&) = delete;
    CitizenBehavior& operator=(const CitizenBehavior&) = delete;

    // ---- members (B4 WORK-a; bodies in citizenbehavior.cpp) ----
    void Update(const System::Time* deltaTime);   // 0x14008c2b0
    void CountResources();                 // 0x140089ed0
    void CheckThresholds(const System::Time* deltaTime); // 0x14008c100
    void CollectStats();                   // 0x14008a730
    // The water search FireWork's Execute (slot 4) runs from the citizen's cell:
    // an expanding ring over the +0x140 coarse index, closest reachable well or
    // water pair wins; writes two terrain positions, returns 1 when it did.
    int64_t FindWater(uint32_t centreCell, int extent, float* outA, float* outB);  // 0x1400881d0
    void ScanWaterCell(Map* map, int coarseCell, int ringX, int ringY, uint32_t srcCell,
                       int* bestCost, int* bestId, WellComponent** bestWell);    // 0x140088730
    void RegisterRawMaterial(ComponentDescription* material);  // 0x140091e60 (defined in Component/rawmaterialcomponent.cpp)

    // ---- members (B4 GAME-a; bodies in Game/citizenlist_serialize.cpp) ----
    void Serialize(IO::Stream& stream);                  // 0x14008f760 (version 6; retail overwrites R8 at 0x14008f78b before any read -- no third parameter, row 1296)
    void PostLoad();                       // 0x14008f580 rebuild the +0x140 coarse water index
    // B4 GAME-a; body in Game/game.cpp: queue a description on the +0x120 list.
    void RemoveEntity(ComponentDescription* entity);   // 0x140091a20
    // ---- the water-source register (B4 COMP-f; bodies in Component/wellcomponent.cpp) ----
    void RemoveCellEntry(int x, int y, int value);                    // 0x1400889e0
    void AddCellEntry(int x, int y, int value);                       // 0x140088a80
    void AddWaterAccess(int cell, int waterCell);                     // 0x140088b80 (the worldgen shore loop)
    void UnregisterWell(Object::InternalPtr<WellComponent>* well);    // 0x140091ba0
    void RegisterWell(Object::InternalPtr<WellComponent>* well);      // 0x140091cb0
    // The zone registry (+0x160; body in Component/zonecomponent.cpp).
    void AddZoneEntity(Object::InternalPtr<ComponentEntity>* entity); // 0x140088e20
    // B4 WORK-b; body in Work/pickup_gate.cpp: does a storage location in the +0x00 list
    // accept `amount` of `resource`? (Pickup::Execute's gate)
    bool HasStorageFor(ComponentDescription* resource, int amount);  // 0x14008d3c0
    // B4 TUT; bodies in Tutorial/tutorial_base.cpp: the entity count of one bucket
    // of the +0x160 (template) / +0x100 (profession) tree, 0 when absent.
    uint32_t CountByTemplate(const ComponentDescription* key) const;    // 0x140092010
    uint32_t CountByProfession(const class Profession* key) const;      // 0x1400910a0

    // ---- B8 ENT: the CitizenBehavior members that were free functions (bodies stay
    // where they were defined; the tree's names, no retail symbol) ----
    // The produced-material count: the source entity's TrackerComponent upsert (when
    // placeNear) and the +0x170 per-category tally (Action/action_base_rawmat.cpp).
    void CountProducedMaterial(ComponentEntity* const* source, char placeNear,
                               ComponentDescription* material, int quantity);           // 0x140088ef0
    // The best storage to drop `material` at from goalCell (Action/action_pf_roots_route.cpp).
    ComponentEntity** PickBestDropTarget(ComponentEntity** best, uint32_t goalCell,
                                         const ComponentDescription* material, int count,
                                         char useCapacity, float* outPos,
                                         const Object::InternalPtr<ComponentEntity>* exclude) const;  // 0x14008d590
    // The worker-seat strategies and their cascade (Component/citizen_employ_cascade.cpp).
    CitizenComponent** FindWorkerToRelocate(CitizenComponent** out, ComponentEntity** availPtr,
                                            Profession* profKey);                       // 0x14008cbf0
    CitizenComponent** FindNearestHousedLaborer(CitizenComponent** out, ComponentEntity** availPtr,
                                                Profession* profKey);                   // 0x14008ca20
    CitizenComponent** FindLaborerFromFullestHouse(CitizenComponent** out, ComponentEntity** availPtr,
                                                   Profession* profKey);                // 0x14008c5f0
    CitizenComponent** FindIdleLaborer(CitizenComponent** out, ComponentEntity** availPtr,
                                       Profession* profKey);                            // 0x14008c4b0
    CitizenComponent** AssignWorker(CitizenComponent** out, ComponentEntity** availPtr,
                                    Profession* profKey);                               // 0x14008c3d0
    // The citizen-AI target searches (Component/citizenai_findhelpers.cpp, citizenai_findtarget.cpp).
    ComponentEntity** FindElderResidence(ComponentEntity** found, uint32_t goalCell, float* outPos);  // 0x14008ec70
    ComponentEntity** FindAdultTarget(ComponentEntity** found, uint32_t goalCell,
                                      ComponentDescription** outComp, float* outPos, uint64_t,
                                      ComponentEntity** preferred);                     // 0x14008e370
    CitizenBehavior* FindBestTarget(ComponentEntity** found, uint32_t goalCellA, uint32_t goalCellB,
                                    ComponentDescription** outComp, float* outPos,
                                    uint32_t category);                                  // 0x14008dd30
    // The town hall's production series sum over [startTick, endTick] (Dialog/townhall_helpers.cpp).
    int QueryProductionStat(int seriesIdx, int startTick, int endTick) const;          // 0x140090f20
    // The debug panel's resource spawn into the town's storages (Game/game.cpp).
    void SpawnResources(uint32_t resourceFlag, int count);                               // 0x140091660
    // The +0x100 profession registry (Game/town_workplace_registry.cpp).
    void RegisterWorkplace(ComponentEntity** workplaceRef, const Profession* profession); // 0x1400895a0
    void* FindAvailableBuilding(void* out, const Profession* professionKey);             // 0x14008d100
    void ShuffleProfessionList(const Profession* professionKey);                         // 0x140089470 (Game/town_workplace_registry.cpp)
    void RemoveWorker(const Profession* profession);                                     // 0x140091180 (Dialog/townhall_helpers.cpp)
    void AddWorker(Profession* profession);                                              // 0x140091130 (Component/citizen_employ_cascade.cpp)
};

static_assert(sizeof(CitizenBehavior) == 0x1C0, "CitizenBehavior is 0x1C0 (MemAlloc in 0x140096b00)");
static_assert(alignof(CitizenBehavior) == 8, "CitizenBehavior alignment");
static_assert(offsetof(CitizenBehavior, storageLocations) == 0x0, "");
static_assert(offsetof(CitizenBehavior, marketLocations) == 0x10, "");
static_assert(offsetof(CitizenBehavior, residences) == 0x20, "");
static_assert(offsetof(CitizenBehavior, tradePosts) == 0x30, "");
static_assert(offsetof(CitizenBehavior, cemeteries) == 0x40, "");
static_assert(offsetof(CitizenBehavior, happinessComponents) == 0x50, "");
static_assert(offsetof(CitizenBehavior, attendanceComponents) == 0x60, "");
static_assert(offsetof(CitizenBehavior, healers) == 0x70, "");
static_assert(offsetof(CitizenBehavior, livestock) == 0x80, "");
static_assert(offsetof(CitizenBehavior, pastures) == 0x90, "");
static_assert(offsetof(CitizenBehavior, citizens) == 0xA0, "");
static_assert(offsetof(CitizenBehavior, herds) == 0xB0, "");
static_assert(offsetof(CitizenBehavior, populations) == 0xC0, "");
static_assert(offsetof(CitizenBehavior, buildComponents) == 0xD0, "");
static_assert(offsetof(CitizenBehavior, wells) == 0xE0, "");
static_assert(offsetof(CitizenBehavior, diseases) == 0xF0, "");
static_assert(offsetof(CitizenBehavior, workplacesByProfession) == 0x100, "");
static_assert(offsetof(CitizenBehavior, knownRawMaterials) == 0x110, "");
static_assert(offsetof(CitizenBehavior, availableFarmTypes) == 0x120, "");
static_assert(offsetof(CitizenBehavior, waterAccessPairs) == 0x130, "");
static_assert(offsetof(CitizenBehavior, coarseWaterIndex) == 0x140, "");
static_assert(offsetof(CitizenBehavior, statHistory) == 0x150, "");
static_assert(offsetof(CitizenBehavior, entitiesByTemplate) == 0x160, "");
static_assert(offsetof(CitizenBehavior, professionResources) == 0x170, "");
static_assert(offsetof(CitizenBehavior, notifyEntries) == 0x180, "");
static_assert(offsetof(CitizenBehavior, pathGraphTimer) == 0x190, "");
static_assert(offsetof(CitizenBehavior, statsTimer) == 0x198, "");
static_assert(offsetof(CitizenBehavior, diseaseRiskTimer) == 0x1A0, "");
static_assert(offsetof(CitizenBehavior, diseaseRisk) == 0x1A8, "");
static_assert(offsetof(CitizenBehavior, totalTradeValue) == 0x1AC, "");
static_assert(offsetof(CitizenBehavior, totalNomadsAccepted) == 0x1B0, "");
static_assert(offsetof(CitizenBehavior, _pad1B4) == 0x1B4, "");
static_assert(sizeof(Collection::Array<int32_t>) == 0x10, "+0x140 bucket stride 0x10 (0x140088730)");
static_assert(offsetof(CitizenBehavior, pathGraph) == 0x1B8, "");
