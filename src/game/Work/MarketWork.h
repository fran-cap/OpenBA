#pragma once
// ============================================================================
// src/game/Work/MarketWork.h — ENDGAME B.3.8 block WRK
//
// MarketWork — the market vendor's work (walk the town's storage, take goods the
// houses need and bring them to the market): a tree of the citizens assigned
// (+0x48, count +0x50; nodes carry a demand array at +0x08). A REAL class with a
// compiler-generated vtable.
//
// RETAIL (B38_SLOTS.md §6): reflected "MarketWork", parent "Work", ti 0x1401c6b70,
//   create 0x14002a8d0 (MemAlloc(0x58) + ctor 0x1400f1f70); createInPlace = the
//   imported Internal::CreateInPlaceObject (B8-13: the tree's in-place build kept)
//   table 0x140161710, 13 slots:
//   [0] 0x14002a910 scalar deleting dtor (the dtor inlined + MemFree on &1)
//   [1] TypeName  [2] Type  [3] 0x1400f1d70 Persist  [4] 0x1400eff80 Execute
//   [5] = Work (0x1400f7540)  [6] 0x1400eff60 OnComplete  [7] 0x1400eff50 OnCancel
//   = `ADD RCX,0x48; JMP 0x14002aaa0` (a member forwarder: the tree's erase-by-key)
//   [8] 0x1400f1ca0 Cleanup  [9] 0x1400efd20 Validate  [10] 0x1400f1d10 GetTarget
//   [11] 0x140029860 GetPriority (`+0x50 > 0`, shared by 13 tables)
//   [12] = Work (0x140006b10)
//   MarketComponent's CreateWork (resourceproduce_creatework.cpp, block MI-B) builds
//   a MarketWork BY HAND and stamps 0x140161710; that registration serves
//   MarketWork::Vtable() (B38 D5).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Work/Work.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Set<T>::Node

class CitizenComponent;
class ComponentDescription;


class MarketWork final : public Work {
    DECLARE_EXE_TYPE(MarketWork)

public:
    MarketWork();                                                   // 0x1400f1f70

    // frees the +0x48 demand tree
    ~MarketWork() override;                                         // [0]  0x14002a910
    void  Persist(IO::Stream& stream) override;                           // [3]  0x1400f1d70
    int   Execute(CitizenComponent** citizenHolder, System::Time* ctx) override;  // [4]  0x1400eff80
    int   OnComplete(CitizenComponent** param2, ComponentEntity** arg2, int arg3) override;  // [6]  0x1400eff60
    int   OnCancel(CitizenComponent** param2, ComponentEntity** arg2, int arg3) override;    // [7]  0x1400eff50
    int   Cleanup() override;                                       // [8]  0x1400f1ca0
    int   Validate() override;                          // [9]  0x1400efd20
    Profession** GetTarget(Profession** outPtr) override;                         // [10] 0x1400f1d10
    int   GetPriority() override;                                   // [11] 0x140029860

    // One entry of a claim's carry list (0x10): the material and how many units the
    // hauler takes (Execute stage 5, 0x1400f18c0..0x1400f1a80; the payload the insert
    // 0x14002a9f0 deep-copies with 0x14000d9d0).
    struct Carry {
        ComponentDescription* resource;  // +0x00 the material's template (a StorageComponent item's)
        int32_t               count;     // +0x08 units
        int32_t               _pad0C;    // +0x0C written 0 with the entry
    };
    // The Array<Carry> header {data, count, capacity} (0x10): a claim's carry list, and
    // the payload the insert 0x14002a9f0 takes in R9.
    struct CarryList {
        Carry*  data;
        int32_t count;
        int32_t capacity;
    };
    // The element of the claim tree (0x18): the claiming citizen (the unsigned sort key)
    // and its carry list, an Array<Carry> {data, count, capacity} (node ctor 0x14002ae50).
    struct Claim {
        Object::InternalPtr<CitizenComponent> citizen;   // +0x00 (Cleanup 0x1400f1ca0 reads its +0x278)
        CarryList         carries;       // +0x08 Array<Carry> {data, count, capacity}
    };
    // Execute's (0x1400eff80) function-local arrays.
    // One demand record per MarketDescription ratio entry (0x20; grown by 0x14002afb0,
    // sorted ascending on `satisfaction` by 0x14002b680). The grow's default-init leaves
    // +0x00 untouched and zeroes +0x04..+0x1f.
    struct Demand {
        uint32_t resourceMask;   // +0x00 the ratio entry's mask
        uint32_t inMarket;       // +0x04 loop A: unitSize * available, this market
        uint32_t inTransit;      // +0x08 loop B: unitSize * qty already claimed
        uint32_t inTown;         // +0x0c loop C: unitSize * available, town-wide
        float    stockedFrac;    // +0x10 stage 3
        float    satisfaction;   // +0x14 stage 3 -- the 0x14002b680 sort key
        uint32_t stage4Scratch;  // +0x18 stage 4 (zeroed per candidate)
        int32_t  shortfall;      // +0x1c stage 3
    };
    struct DemandList {          // the Array<Demand> header 0x14002afb0 takes in RCX
        Demand*  data;
        int32_t  count;
        int32_t  capacity;
    };
    // One take-list record of stage 5 (0x18; 0x1400f1425..0x1400f16a0, sorted
    // ascending on the signed `rank` by 0x14002ba70).
    struct Take {
        ComponentDescription* resource;  // +0x00
        int32_t rank;                    // +0x08
        int32_t avail;                   // +0x0c
        int32_t record;                  // +0x10 demand-record index
        int32_t _pad14;                  // +0x14 written 0
    };
    struct TakeList {
        Take*   data;
        int32_t count;
        int32_t capacity;
    };

    // The claim tree's 0x38-byte node: parent +0x18, left +0x20, right +0x28, balance +0x30.
    using ClaimNode = Collection::Set<Claim>::Node;

    // The claim tree's header, Collection::Set<Claim>'s {root, count}, as a POD (no ctor,
    // no dtor: H5, Set has a dtor; the class's ctor zeroes it and its dtor frees the claims
    // and their carry lists by hand, as retail's 0x14002a910 / 0x14002af40 do). The tree
    // entry points (0x14002a9f0, 0x14002aaa0, 0x14002ac40, 0x14002b290) take it.
    struct ClaimTree {
        ClaimNode* root;         // +0x00
        int32_t    count;        // +0x08 (GetPriority 0x140029860 `CMP [RCX+0x50],EAX; SETG`)
    };

    // --- MarketWork fields (+0x48) ---
    ClaimTree _claims;          // +0x48: the claim tree {root +0x48, count +0x50}
};

static_assert(sizeof(MarketWork::Carry) == 0x10, "MarketWork carry entry stride 0x10 (Execute 0x1400f18c0)");
static_assert(offsetof(MarketWork::Carry, resource) == 0x0, "Carry::resource");
static_assert(offsetof(MarketWork::Carry, _pad0C) == 0xC, "Carry::_pad0C");
static_assert(offsetof(MarketWork::Carry, count) == 0x08, "MarketWork::Carry::count @ +0x08");
static_assert(sizeof(MarketWork::Demand) == 0x20, "demand record stride 0x20 (0x14002afb0 SHL 5)");
static_assert(offsetof(MarketWork::Demand, resourceMask) == 0x0, "Demand::resourceMask");
static_assert(offsetof(MarketWork::Demand, inMarket) == 0x4, "Demand::inMarket");
static_assert(offsetof(MarketWork::Demand, inTransit) == 0x8, "Demand::inTransit");
static_assert(offsetof(MarketWork::Demand, inTown) == 0xC, "Demand::inTown");
static_assert(offsetof(MarketWork::Demand, stockedFrac) == 0x10, "Demand::stockedFrac");
static_assert(offsetof(MarketWork::Demand, stage4Scratch) == 0x18, "Demand::stage4Scratch");
static_assert(offsetof(MarketWork::Demand, satisfaction) == 0x14, "Demand sort key @ +0x14 (0x14002bde0)");
static_assert(offsetof(MarketWork::Demand, shortfall) == 0x1c, "Demand::shortfall @ +0x1c");
static_assert(sizeof(MarketWork::DemandList) == 0x10, "demand list header 0x10");
static_assert(offsetof(MarketWork::DemandList, data) == 0x0, "DemandList::data");
static_assert(offsetof(MarketWork::DemandList, count) == 0x08, "DemandList::count @ +0x08");
static_assert(offsetof(MarketWork::DemandList, capacity) == 0x0c, "DemandList::capacity @ +0x0c");
static_assert(sizeof(MarketWork::Take) == 0x18, "take record stride 0x18");
static_assert(offsetof(MarketWork::Take, resource) == 0x0, "Take::resource");
static_assert(offsetof(MarketWork::Take, avail) == 0xC, "Take::avail");
static_assert(offsetof(MarketWork::Take, _pad14) == 0x14, "Take::_pad14");
static_assert(offsetof(MarketWork::Take, rank) == 0x08, "Take sort key @ +0x08");
static_assert(offsetof(MarketWork::Take, record) == 0x10, "Take::record @ +0x10");
static_assert(sizeof(MarketWork::TakeList) == 0x10, "take list header 0x10");
static_assert(offsetof(MarketWork::TakeList, data) == 0x0, "TakeList::data");
static_assert(offsetof(MarketWork::TakeList, count) == 0x8, "TakeList::count");
static_assert(offsetof(MarketWork::TakeList, capacity) == 0xC, "TakeList::capacity");
static_assert(sizeof(MarketWork::CarryList) == 0x10, "MarketWork carry list header 0x10");
static_assert(offsetof(MarketWork::CarryList, data) == 0x0, "CarryList::data");
static_assert(offsetof(MarketWork::CarryList, capacity) == 0xC, "CarryList::capacity");
static_assert(offsetof(MarketWork::CarryList, count) == 0x08, "MarketWork::CarryList::count @ +0x08");
static_assert(sizeof(MarketWork::Claim) == 0x18, "MarketWork claim element 0x18 (node 0x38, 0x14002ae50)");
static_assert(offsetof(MarketWork::Claim, citizen) == 0x0, "Claim::citizen");
static_assert(offsetof(MarketWork::Claim, carries) == 0x08, "MarketWork::Claim::carries @ +0x08");
static_assert(sizeof(MarketWork::ClaimNode) == 0x38, "MarketWork claim node 0x38 (MemAlloc 0x14002ae70)");
static_assert(offsetof(MarketWork::ClaimNode, parent) == 0x18, "claim node parent @ +0x18 (0x14002aeb5)");
static_assert(offsetof(MarketWork::ClaimNode, left) == 0x20, "claim node left @ +0x20 (0x14002aeb9)");
static_assert(offsetof(MarketWork::ClaimNode, right) == 0x28, "claim node right @ +0x28 (0x14002aebd)");
static_assert(offsetof(MarketWork::ClaimNode, balance) == 0x30, "claim node balance @ +0x30 (0x14002aec1)");

// 0x14002af40 (work_leaf_market.cpp): post-order free of a claim subtree (each node's carry
// list, then the node). `ctx` is the tree root slot retail threads through RCX, unread.
void WorkLeaf_MarketTreeCleanup_2af40(MarketWork::ClaimNode* const* ctx, MarketWork::ClaimNode* node);

static_assert(sizeof(MarketWork) == 0x58, "MarketWork is 0x58 (retail create MemAlloc)");
static_assert(alignof(MarketWork) == 8, "MarketWork is 8-aligned");
static_assert(offsetof(MarketWork, _claims) == 0x48, "MarketWork +0x48");
static_assert(offsetof(MarketWork, _claims.count) == 0x50, "MarketWork +0x50");
static_assert(sizeof(MarketWork::ClaimTree) == 0x10, "Set header {root, count} + pad");
static_assert(offsetof(MarketWork::ClaimTree, root) == 0x0, "ClaimTree::root");
static_assert(offsetof(MarketWork::ClaimTree, count) == 0x8, "ClaimTree::count");
