#pragma once
// ============================================================================
// src/game/Work/HarvestArea.h — ENDGAME B.3.8 block WRK
//
// HarvestArea — the crop-field harvest work: a BST of the cells being harvested
// (+0x48, count +0x50) and a per-cell validity byte array (+0x58 / +0x60). A REAL
// class with a compiler-generated vtable.
//
// RETAIL (B38_SLOTS.md §6): reflected "HarvestArea", parent "AutoPickupWork",
//   create 0x14002a460 (MemAlloc(0x68) + ctor); createInPlace = the imported
//   Internal::CreateInPlaceObject (B8-13: the tree's in-place build kept)
//   table 0x1401614a0, 13 slots:
//   [0] 0x14002a4d0 scalar deleting dtor  [1] TypeName  [2] Type
//   [3] 0x1400ed350 Persist  [4] 0x1400eccd0 Execute  [5] = Work (0x1400f7540)
//   [6] 0x1400ecc20 OnComplete  [7] 0x1400ecb60 OnCancel  [8] 0x1400ed270 Cleanup
//   [9] 0x1400ec930 Validate  [10] 0x1400ed2f0 GetTarget
//   [11] 0x140029860 GetPriority (`+0x50 > 0`, shared by 13 tables)  [12] = Work
//   CropFieldComponent's state machine (cropfieldcomponent_states.cpp, block MI-B)
//   builds the work BY HAND and stamps 0x1401614a0; that registration serves
//   HarvestArea::Vtable() (B38 D5). The default ctor here builds what the tree's
//   factory built (Work_GenericCtor: base init, leaf fields zeroed).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Work/AutoPickupWork.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Set<T>::Node

class CitizenComponent;

class HarvestArea final : public AutoPickupWork {
    DECLARE_EXE_TYPE(HarvestArea)

public:
    HarvestArea();                                                  // (Work_GenericCtor shape)

    // frees the validity array and the cell BST
    ~HarvestArea() override;                                        // [0]  0x14002a4d0
    void  Persist(IO::Stream& stream) override;                           // [3]  0x1400ed350
    int   Execute(CitizenComponent** entityIBP, System::Time* context) override;  // [4]  0x1400eccd0
    int   OnComplete(CitizenComponent** param2, ComponentEntity** param3, int arg3) override;  // [6]  0x1400ecc20
    int   OnCancel(CitizenComponent** param2, ComponentEntity** param3, int arg3) override;    // [7]  0x1400ecb60
    int   Cleanup() override;                                       // [8]  0x1400ed270
    int   Validate() override;                          // [9]  0x1400ec930
    Profession** GetTarget(Profession** outPtr) override;                         // [10] 0x1400ed2f0
    int   GetPriority() override;                                   // [11] 0x140029860

    // --- HarvestArea fields (+0x48) ---
    // The element of the claim tree (0x10): the crop-field slot index (the int key) and
    // the claiming citizen (the cell-BST helpers 0x140029a40 / 0x14001ca60 / 0x140029de0;
    // Execute 0x1400eccd0 stores the key at +0 and the citizen at +8).
    struct Claim {
        int32_t           slot;      // +0x00
        int32_t           _pad04;    // +0x04 written 0 with the key
        Object::InternalPtr<CitizenComponent> citizen;   // +0x08 (Cleanup reads its +0x278)
    };
    // Its 0x30-byte node: parent +0x10, left +0x18, right +0x20.
    using ClaimNode = Collection::Set<Claim>::Node;

    // +0x48: Collection::Set<Claim> (0x30-byte nodes: the cell-BST helpers 0x140029a40 /
    // 0x14001ca60 / 0x140029de0). A real member: the ctor 0x1400ed4c0 zeroes root (qword) and
    // count (dword) under an EH state; the dtor 0x1400ed410 frees the +0x58 array, THEN runs the
    // set's inlined Clear (0x14000e320 on the root's +0x18 / +0x20, MemFree, zero root /
    // count): the dtor body, then this member's ~Set. `_cells._count` (+0x50) is the
    // claim count (0x140029860 `CMP [RCX+0x50]; SETG`: signed). The Set ctor also zeroes
    // its +0x0c pad (+0x54), which retail leaves (known_port_issues row 640).
    using CellSet = Collection::Set<Claim>;
    CellSet _cells;
    // +0x58: Collection::Array<bool> {data, count +0x60, capacity +0x64}, one flag per
    // crop-field slot (Persist 0x1400ed350 serializes it through the bool-array door 0x140151458 with
    // the int count at +0x60). A real member: the ctor 0x1400ed4c0 zeroes +0x58 / +0x60 as two
    // qwords after the set; the dtor 0x1400ed410 restamps, MemFree(+0x58), +0x58 = +0x60 = 0,
    // THEN clears the set: reverse declaration order (~Array before ~Set). It grows through
    // 0x14000a590 (stride 1, align 1, NO tail write: the trait row of a trivial bool).
    using SlotFlags = Collection::Array<bool>;
    SlotFlags _slotsAvailable;
};

static_assert(sizeof(HarvestArea::Claim) == 0x10, "HarvestArea claim element 0x10");
static_assert(offsetof(HarvestArea::Claim, slot) == 0x0, "Claim::slot");
static_assert(offsetof(HarvestArea::Claim, _pad04) == 0x4, "Claim::_pad04");
static_assert(offsetof(HarvestArea::Claim, citizen) == 0x8, "Claim::citizen");
static_assert(sizeof(HarvestArea::ClaimNode) == 0x30, "HarvestArea claim node 0x30");
static_assert(offsetof(HarvestArea::ClaimNode, parent) == 0x10, "claim node parent @ +0x10");
static_assert(offsetof(HarvestArea::ClaimNode, left) == 0x18, "claim node left @ +0x18");
static_assert(offsetof(HarvestArea::ClaimNode, right) == 0x20, "claim node right @ +0x20");

static_assert(sizeof(HarvestArea) == 0x68, "HarvestArea is 0x68 (retail create MemAlloc)");
static_assert(alignof(HarvestArea) == 8, "HarvestArea is 8-aligned");
static_assert(offsetof(HarvestArea, _cells) == 0x48, "HarvestArea +0x48");
static_assert(offsetof(HarvestArea, _cells) + offsetof(HarvestArea::CellSet, _count) == 0x50, "HarvestArea +0x50");
static_assert(offsetof(HarvestArea, _slotsAvailable) == 0x58, "HarvestArea +0x58");
static_assert(offsetof(HarvestArea, _slotsAvailable) + offsetof(HarvestArea::SlotFlags, _size) == 0x60, "HarvestArea +0x60");
static_assert(offsetof(HarvestArea, _slotsAvailable) + offsetof(HarvestArea::SlotFlags, _capacity) == 0x64, "HarvestArea +0x64");
