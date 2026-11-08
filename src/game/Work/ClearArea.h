#pragma once
// ============================================================================
// src/game/Work/ClearArea.h — ENDGAME B.3.8 block WRK
//
// ClearArea — the "clear this footprint" work of a construction / orchard site:
// a BST of the entities to cut (+0x48) and one of the ground cells to clear
// (+0x58). A REAL class with a compiler-generated vtable.
//
// RETAIL (B38_SLOTS.md §6): reflected "ClearArea", parent "Work", ti 0x1401c68d0,
//   create 0x140029820 (MemAlloc(0x78) + ctor 0x1400e68d0); createInPlace = the
//   imported Internal::CreateInPlaceObject (B8-13: the tree's in-place build kept)
//   table 0x140161110, 13 slots:
//   [0] 0x140029870 scalar deleting dtor (the dtor 0x1400e6810 + MemFree on &1)
//   [1] 0x140029810 TypeName  [2] 0x140029800 Type  [3] 0x1400e6740 Persist
//   [4] 0x1400e5ee0 Execute   [5] = Work (0x1400f7540)  [6] 0x1400e5e30 OnComplete
//   [7] 0x1400e5e00 OnCancel  [8] 0x1400e6650 Cleanup   [9] 0x1400e5b10 Validate
//   [10] 0x1400e6720 GetTarget  [11] 0x140029860 GetPriority (shared, 13 tables)
//   [12] = Work (0x140006b10)
//   ctors: 0x1400e68d0 (default); 0x1400e6960 (the parameterised one: Work_Ctor25
//   on the entity, then the leaf stores) — clearareacomponent_states.cpp and the
//   orchard build it with `::new (p) ClearArea(…)` (ENDGAME B.3.9 EXA).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Work/Work.h"
#include "Object/exe_type.h"

class ComponentDescription;


class ClearArea final : public Work {
    DECLARE_EXE_TYPE(ClearArea)

public:
    ClearArea();                                                    // 0x1400e68d0
    // 0x1400e6960: `Work(pEntityIBP)` (retail's CALL Work_Ctor25 on a copy of
    // *pEntityIBP), then +0x48..+0x60 = 0, +0x68 = descRef (raw), +0x70..+0x72.
    ClearArea(ComponentEntity* const* pEntityIBP, ComponentDescription* descRef, bool clearTrees,
              bool clearGround, bool clearBuildings);

    // 0x1400e6810: frees both BSTs
    ~ClearArea() override;                                          // [0]  0x140029870
    void  Persist(IO::Stream& stream) override;                           // [3]  0x1400e6740
    int   Execute(CitizenComponent** actionStack, System::Time* time) override;  // [4]  0x1400e5ee0
    int   OnComplete(CitizenComponent** citizen, ComponentEntity** treeEntry, int cellIndex) override;  // [6]  0x1400e5e30
    int   OnCancel(CitizenComponent** citizen, ComponentEntity** treeEntry, int arg3) override;       // [7]  0x1400e5e00
    int   Cleanup() override;                                       // [8]  0x1400e6650
    int   Validate() override;                      // [9]  0x1400e5b10
    Profession** GetTarget(Profession** retbuf) override;                         // [10] 0x1400e6720
    int   GetPriority() override;                                   // [11] 0x140029860

    // --- ClearArea fields (+0x48) ---
    // +0x48 / +0x58: two Collection::Set members, entity -> citizen (0x30 nodes,
    // 0x140029b40) and cell -> citizen (0x30 nodes, 0x140029a40). Retail's ctor 0x1400e6960
    // zeroes each (root qword, count dword) under its own EH member state, +0x48 first; the
    // dtor 0x1400e6810 clears +0x58 first, then +0x48 (0x14000e320 on +0x18 / +0x20, MemFree
    // of the root, zero root / count): the members' reverse order. `._count` (+0x50 / +0x60)
    // are the tree / ground counts. Each Set ctor also zeroes its +0x0c pad (+0x54 / +0x64),
    // which retail leaves (known_port_issues row 640).
    using EntitySet = Collection::Set<WorkEntityAssignment>;
    using CellSet   = Collection::Set<WorkCellAssignment>;
    EntitySet _entities;         // +0x48
    CellSet   _cells;            // +0x58
    ComponentDescription* _targetFilter; // +0x68: ExternalPtr<ComponentDescription> filter (Persist 0x1400e6740
                                    //        serializes it by the ExternalPtr body; Execute compares entity +0x28)
    bool     _clearTrees;           // +0x70: bool — clear trees/resources
    bool     _clearGround;           // +0x71: bool — clear ground (version >= 1)
    bool     _clearPiles;           // +0x72: bool — clear buildings (version >= 1)
    uint8_t  _pad73[5];          // +0x73: padding
};

static_assert(sizeof(ClearArea) == 0x78, "ClearArea is 0x78 (retail create MemAlloc)");
static_assert(offsetof(ClearArea, _pad73) == 0x73, "ClearArea::_pad73");
static_assert(alignof(ClearArea) == 8, "ClearArea is 8-aligned");
static_assert(offsetof(ClearArea, _entities) == 0x48, "ClearArea +0x48");
static_assert(offsetof(ClearArea, _entities) + offsetof(ClearArea::EntitySet, _count) == 0x50, "ClearArea +0x50");
static_assert(offsetof(ClearArea, _cells) == 0x58, "ClearArea +0x58");
static_assert(offsetof(ClearArea, _cells) + offsetof(ClearArea::CellSet, _count) == 0x60, "ClearArea +0x60");
static_assert(offsetof(ClearArea, _targetFilter) == 0x68, "ClearArea +0x68");
static_assert(offsetof(ClearArea, _clearTrees) == 0x70, "ClearArea +0x70");
static_assert(offsetof(ClearArea, _clearGround) == 0x71, "ClearArea +0x71");
static_assert(offsetof(ClearArea, _clearPiles) == 0x72, "ClearArea +0x72");
