#pragma once
// ============================================================================
// src/game/Work/HarvestOrchard.h — ENDGAME B.3.8 block WRK
//
// HarvestOrchard — the orchard harvest work: a BST of the trees being picked
// (+0x48, count +0x50) and a per-orchard-slot byte array (+0x58 / +0x60: 1 = a
// tree to pick). A REAL class with a compiler-generated vtable.
//
// RETAIL (B38_SLOTS.md §6): reflected "HarvestOrchard", parent "AutoPickupWork",
//   create 0x14002a520 (MemAlloc(0x68) + ctor); createInPlace = the imported
//   Internal::CreateInPlaceObject (B8-13: the tree's in-place build kept)
//   table 0x140161520, 13 slots: [0] scalar deleting dtor  [1] TypeName  [2] Type
//   [3] 0x1400edea0 Persist  [4] Execute  [5] = Work (0x1400f7540)  [6] OnComplete
//   [7] OnCancel  [8] Cleanup  [9] Validate  [10] GetTarget  [11] 0x140029860
//   GetPriority (`+0x50 > 0`, shared by 13 tables)  [12] = Work (0x140006b10)
//   ctors: the default one (the tree built it with Work_GenericCtor: base init, leaf
//   fields zeroed — kept) and the from-entity ctor 0x1400ee060 (harvestorchard_ctor.cpp),
//   which OrchardComponent's state 11 (orchardcomponent_states.cpp, block MI-B) calls
//   by name on its MemAlloc(0x68, 8) block (B9). Nothing outside this block stamps
//   0x140161520.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Work/AutoPickupWork.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Set<T>::Node

class CitizenComponent;


class HarvestOrchard final : public AutoPickupWork {
    DECLARE_EXE_TYPE(HarvestOrchard)

public:
    HarvestOrchard();                                               // (Work_GenericCtor shape)
    explicit HarvestOrchard(ComponentEntity* const* entityIBP);                      // 0x1400ee060

    // frees the slot array and the BST
    ~HarvestOrchard() override;                                     // [0]
    void  Persist(IO::Stream& stream) override;                           // [3]  0x1400edea0
    int   Execute(CitizenComponent** entityIBP, System::Time* context) override;  // [4]
    int   OnComplete(CitizenComponent** param2, ComponentEntity** param3, int param4) override;  // [6]
    int   OnCancel(CitizenComponent** param2, ComponentEntity** param3, int param4) override;    // [7]
    int   Cleanup() override;                                       // [8]
    int   Validate() override;                          // [9]
    Profession** GetTarget(Profession** outPtr) override;                         // [10]
    int   GetPriority() override;                                   // [11] 0x140029860

    // --- HarvestOrchard fields (+0x48) ---
    // The element of the claim tree (0x10): the orchard slot index (the int key) and the
    // claiming citizen (the cell-BST helpers 0x140029a40 / 0x14001ca60 / 0x140029de0).
    struct Claim {
        int32_t           slot;      // +0x00
        int32_t           _pad04;    // +0x04 written 0 with the key
        Object::InternalPtr<CitizenComponent> citizen;   // +0x08 (Cleanup reads its +0x278)
    };
    // Its 0x30-byte node: parent +0x10, left +0x18, right +0x20.
    using ClaimNode = Collection::Set<Claim>::Node;

    // +0x48: Collection::Set<Claim> (0x30-byte nodes: the cell-BST helpers 0x140029a40 /
    // 0x14001ca60 / 0x140029de0). A real member: the ctor 0x1400ee060 zeroes root (qword) and
    // count (dword) under an EH state; the dtor 0x1400edf60 frees the +0x58 array, THEN runs the
    // set's inlined Clear (0x14000e320 on the root's +0x18 / +0x20, MemFree, zero root /
    // count): the dtor body, then this member's ~Set. `_cells._count` (+0x50) is the
    // claim count (0x140029860 `CMP [RCX+0x50]; SETG`: signed). The Set ctor also zeroes
    // its +0x0c pad (+0x54), which retail leaves (known_port_issues row 640).
    using CellSet = Collection::Set<Claim>;
    CellSet _cells;
    // +0x58: Collection::Array<bool> {data, count +0x60, capacity +0x64}, one flag per
    // orchard slot (Persist 0x1400edea0 serializes it through the bool-array door 0x140151458 with
    // the int count at +0x60). A real member: the ctor 0x1400ee060 zeroes +0x58 / +0x60 as two
    // qwords after the set; the dtor 0x1400edf60 restamps, MemFree(+0x58), +0x58 = +0x60 = 0,
    // THEN clears the set: reverse declaration order (~Array before ~Set). It grows through
    // 0x14000a590 (stride 1, align 1, NO tail write: the trait row of a trivial bool).
    using SlotFlags = Collection::Array<bool>;
    SlotFlags _slotsAvailable;
};

static_assert(sizeof(HarvestOrchard::Claim) == 0x10, "HarvestOrchard claim element 0x10");
static_assert(offsetof(HarvestOrchard::Claim, slot) == 0x0, "Claim::slot");
static_assert(offsetof(HarvestOrchard::Claim, _pad04) == 0x4, "Claim::_pad04");
static_assert(offsetof(HarvestOrchard::Claim, citizen) == 0x8, "Claim::citizen");
static_assert(sizeof(HarvestOrchard::ClaimNode) == 0x30, "HarvestOrchard claim node 0x30");
static_assert(offsetof(HarvestOrchard::ClaimNode, parent) == 0x10, "claim node parent @ +0x10");
static_assert(offsetof(HarvestOrchard::ClaimNode, left) == 0x18, "claim node left @ +0x18");
static_assert(offsetof(HarvestOrchard::ClaimNode, right) == 0x20, "claim node right @ +0x20");

static_assert(sizeof(HarvestOrchard) == 0x68, "HarvestOrchard is 0x68 (retail create MemAlloc)");
static_assert(alignof(HarvestOrchard) == 8, "HarvestOrchard is 8-aligned");
static_assert(offsetof(HarvestOrchard, _cells) == 0x48, "HarvestOrchard +0x48");
static_assert(offsetof(HarvestOrchard, _cells) + offsetof(HarvestOrchard::CellSet, _count) == 0x50, "HarvestOrchard +0x50");
static_assert(offsetof(HarvestOrchard, _slotsAvailable) == 0x58, "HarvestOrchard +0x58");
static_assert(offsetof(HarvestOrchard, _slotsAvailable) + offsetof(HarvestOrchard::SlotFlags, _size) == 0x60, "HarvestOrchard +0x60");
static_assert(offsetof(HarvestOrchard, _slotsAvailable) + offsetof(HarvestOrchard::SlotFlags, _capacity) == 0x64, "HarvestOrchard +0x64");
