#pragma once
// ============================================================================
// src/game/Work/PlantOrchard.h — ENDGAME B.3.8 block WRK
//
// PlantOrchard — the orchard planting work: a BST of the slots being planted
// (+0x48, count +0x50), a per-orchard-slot byte array (+0x58 / +0x60 / +0x64) and
// the tree type chosen in the orchard UI (ExternalPtr +0x68). A REAL class with a
// compiler-generated vtable.
//
// RETAIL (B38_SLOTS.md §6): reflected "PlantOrchard", parent "Work", ti 0x1401c6c50,
//   create 0x14002c500 (MemAlloc(0x70) + ctor 0x1400f50b0); createInPlace = the
//   imported Internal::CreateInPlaceObject (B8-13: the tree's in-place build kept)
//   table 0x140161910, 13 slots:
//   [0] 0x14002c540 scalar deleting dtor (the dtor 0x1400f5010 + MemFree on &1)
//   [1] TypeName  [2] Type  [3] 0x1400f4f50 Persist  [4] 0x1400f4af0 Execute
//   [5] = Work (0x1400f7540)  [6] 0x1400f4a90 OnComplete  [7] 0x1400f4a20 OnCancel
//   [8] 0x1400f4e70 Cleanup  [9] 0x1400f47f0 Validate  [10] 0x1400f4ef0 GetTarget
//   [11] 0x140029860 GetPriority (`+0x50 > 0`, shared by 13 tables)
//   [12] = Work (0x140006b10)
//   ctors: the default one 0x1400f50b0 (deserialize/factory, +0x68 null) and the
//   from-entity one 0x1400f5140 (plantorchard_ctor.cpp) that OrchardComponent's
//   states 9 and 12 (orchardcomponent_states.cpp, block MI-B) call by name on their
//   MemAlloc(0x70, 8) block (B9). Nothing outside this block reads 0x140161910 back.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Work/Work.h"
#include "Object/exe_type.h"

class ComponentDescription;


class PlantOrchard final : public Work {
    DECLARE_EXE_TYPE(PlantOrchard)

public:
    PlantOrchard();                                                 // 0x1400f50b0
    PlantOrchard(ComponentEntity* const* entityIBP, ComponentDescription* treeType);   // 0x1400f5140

    // 0x1400f5010: frees the slot array and the BST
    ~PlantOrchard() override;                                       // [0]  0x14002c540
    void  Persist(IO::Stream& stream) override;                           // [3]  0x1400f4f50
    int   Execute(CitizenComponent** entityIBP, System::Time* context) override;  // [4]  0x1400f4af0
    int   OnComplete(CitizenComponent** param2, ComponentEntity** param3, int param4) override;  // [6]  0x1400f4a90
    int   OnCancel(CitizenComponent** param2, ComponentEntity** param3, int param4) override;    // [7]  0x1400f4a20
    int   Cleanup() override;                                       // [8]  0x1400f4e70
    int   Validate() override;                          // [9]  0x1400f47f0
    Profession** GetTarget(Profession** outPtr) override;                         // [10] 0x1400f4ef0
    int   GetPriority() override;                                   // [11] 0x140029860

    // --- PlantOrchard fields (+0x48) ---
    // +0x48: Collection::Set<WorkCellAssignment> (0x30-byte nodes: the cell-BST helpers 0x140029a40 /
    // 0x14001ca60 / 0x140029de0). A real member: the ctor 0x1400f50b0 zeroes root (qword) and
    // count (dword) under an EH state; the dtor 0x1400f5010 frees the +0x58 array, THEN runs the
    // set's inlined Clear (0x14000e320 on the root's +0x18 / +0x20, MemFree, zero root /
    // count): the dtor body, then this member's ~Set. `_cells._count` (+0x50) is the
    // claim count (0x140029860 `CMP [RCX+0x50]; SETG`: signed). The Set ctor also zeroes
    // its +0x0c pad (+0x54), which retail leaves (known_port_issues row 640).
    using CellSet = Collection::Set<WorkCellAssignment>;
    CellSet _cells;
    // +0x58: Collection::Array<bool> {data, count +0x60, capacity +0x64}, one flag per
    // orchard slot (1 = planted; Persist streams it through the bool-array overload
    // 0x140151458). It grows through 0x14000a590 (stride 1, align 1, NO tail write: the
    // trait row of a trivial bool). A real member: the ctor 0x1400f50b0 zeroes +0x58 /
    // +0x60 as two qwords after the set; the dtor 0x1400f5010 restamps, MemFree(+0x58),
    // +0x58 = +0x60 = 0, THEN clears the set: reverse declaration order (~Array before ~Set).
    using SlotFlags = Collection::Array<bool>;
    SlotFlags _claimedSlots;
    ComponentDescription* _treeType;   // +0x68: ExternalPtr<ComponentDescription> (0x140002340): the
                                      //        TREE TYPE template chosen in OrchardUI's combo (copied
                                      //        from OrchardComponent+0x60). NOT the OrchardDescription.
};

static_assert(sizeof(PlantOrchard) == 0x70, "PlantOrchard is 0x70 (retail create MemAlloc)");
static_assert(alignof(PlantOrchard) == 8, "PlantOrchard is 8-aligned");
static_assert(offsetof(PlantOrchard, _cells) == 0x48, "PlantOrchard +0x48");
static_assert(offsetof(PlantOrchard, _cells) + offsetof(PlantOrchard::CellSet, _count) == 0x50, "PlantOrchard +0x50");
static_assert(offsetof(PlantOrchard, _claimedSlots) == 0x58, "PlantOrchard +0x58");
static_assert(offsetof(PlantOrchard, _claimedSlots) + offsetof(PlantOrchard::SlotFlags, _size) == 0x60, "PlantOrchard +0x60");
static_assert(offsetof(PlantOrchard, _claimedSlots) + offsetof(PlantOrchard::SlotFlags, _capacity) == 0x64, "PlantOrchard +0x64");
static_assert(offsetof(PlantOrchard, _treeType) == 0x68, "PlantOrchard +0x68");
