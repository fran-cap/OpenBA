#pragma once
// ============================================================================
// src/game/Work/BuildStructure.h — ENDGAME B.3.8 block WRK
//
// BuildStructure — the construction-labour work of a building site: one byte per
// build position (free / claimed) and a BST of the citizens holding one. A REAL
// class with a compiler-generated vtable.
//
// RETAIL (B38_SLOTS.md §6): reflected "BuildStructure", parent "Work", ti 0x1401c6898,
//   create 0x140029630 (MemAlloc(0x70) + ctor 0x1400e5a90); createInPlace = the
//   imported Internal::CreateInPlaceObject (B8-13: the tree's in-place build kept)
//   table 0x1401610a0, 13 slots:
//   [0] 0x140029680 scalar deleting dtor (the dtor 0x1400e5920 + MemFree on &1)
//   [1] 0x140029620 TypeName  [2] 0x140029610 Type  [3] 0x1400e5850 Persist
//   [4] 0x1400e4fc0 Execute   [5] = Work (0x1400f7540)  [6] 0x1400e4f80 OnComplete
//   [7] 0x1400e4f40 OnCancel  [8] 0x1400e57a0 Cleanup   [9] 0x1400e4be0 Validate
//   [10] 0x1400e5830 GetTarget  [11] 0x140029670 GetPriority  [12] = Work (0x140006b10)
//   ctors: 0x1400e5a90 (default) and 0x1400e59c0 (entity + alt-anim flag; the one
//   BuildComponent's state-8 enter uses).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Work/Work.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Set<T>::Node

class CitizenComponent;


class BuildStructure final : public Work {
    DECLARE_EXE_TYPE(BuildStructure)

public:
    BuildStructure();                                               // 0x1400e5a90
    BuildStructure(ComponentEntity* const* entityIBP, bool altAnim);               // 0x1400e59c0

    // 0x1400e5920: the compiler's chain over the two members (set, then +0x50)
    ~BuildStructure() override;                                     // [0]  0x140029680
    void  Persist(IO::Stream& stream) override;                           // [3]  0x1400e5850
    int   Execute(CitizenComponent** citizenPtr, System::Time* context) override;  // [4]  0x1400e4fc0
    int   OnComplete(CitizenComponent** citizenIBP, ComponentEntity** unused, int slotIndex) override;  // [6]  0x1400e4f80
    int   OnCancel(CitizenComponent** citizenIBP, ComponentEntity** unused, int slotIndex) override;    // [7]  0x1400e4f40
    int   Cleanup() override;                                       // [8]  0x1400e57a0
    int   Validate() override;                      // [9]  0x1400e4be0
    Profession** GetTarget(Profession** retBuf) override;                         // [10] 0x1400e5830
    int   GetPriority() override;                                   // [11] 0x140029670

    // --- BuildStructure fields (+0x48) ---
    // Note: ctor doesn't init +0x48/+0x4C (set by Serialize or caller)
    bool     _isDemolition;           // +0x48: bool — serialized flag (has initial setup?)
    uint8_t  _pad49[3];          // +0x49: padding
    int32_t  _scanIndex;           // +0x4C: current slot index (round-robin for assignment)
    // +0x50: Collection::Array<uint8_t> {data, count +0x58, capacity +0x5C}: slot
    // availability, one byte per build point (0 = free, 1 = busy; Persist 0x1400e5850 uses
    // the unsigned-char array overload). +0x60: Collection::Set<InternalPtr<CitizenComponent>>
    // (node 0x28: element, parent +0x08, left +0x10, right +0x18; Cleanup 0x1400e57a0 reads
    // the element's +0x278); its count +0x68 is GetPriority's (0x140029670 `> 0`). Real
    // members: the ctors 0x1400e5a90 / 0x1400e59c0 zero +0x50 / +0x58 as two qwords and the
    // set's root (qword) and count (dword) under an EH state; the dtor 0x1400e5920 clears the
    // set FIRST, then frees +0x50 (data = 0, +0x58 = 0 qword): the reverse of this order. The
    // Set's ctor also zeroes its +0x0c pad (+0x6C), which retail's leaves (row 640).
    using CitizenSet     = Collection::Set<CitizenComponent*>;
    using AssignmentNode = CitizenSet::Node;
    Collection::Array<uint8_t> _slots;   // +0x50
    CitizenSet _citizens;                // +0x60
};

static_assert(sizeof(BuildStructure) == 0x70, "BuildStructure is 0x70 (retail create MemAlloc)");
static_assert(offsetof(BuildStructure, _pad49) == 0x49, "BuildStructure::_pad49");
static_assert(alignof(BuildStructure) == 8, "BuildStructure is 8-aligned");
static_assert(offsetof(BuildStructure, _isDemolition) == 0x48, "BuildStructure +0x48");
static_assert(offsetof(BuildStructure, _scanIndex) == 0x4c, "BuildStructure +0x4c");
static_assert(offsetof(BuildStructure, _slots) == 0x50, "BuildStructure +0x50");
static_assert(offsetof(BuildStructure, _slots) + offsetof(Collection::Array<uint8_t>, _size) == 0x58, "BuildStructure +0x58");
static_assert(offsetof(BuildStructure, _slots) + offsetof(Collection::Array<uint8_t>, _capacity) == 0x5c, "BuildStructure +0x5c");
static_assert(offsetof(BuildStructure, _citizens) == 0x60, "BuildStructure +0x60");
static_assert(offsetof(BuildStructure, _citizens) + offsetof(BuildStructure::CitizenSet, _count) == 0x68, "BuildStructure +0x68");
