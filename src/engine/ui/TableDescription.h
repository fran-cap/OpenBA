#pragma once
// ============================================================================
// src/engine/ui/TableDescription.h — UI::TableDescription, the REAL C++
// class (ENDGAME B.3 step 6, block C4)
// ============================================================================
//
// PARENT `UI::ElementDescription` — READ, not inferred:
//     ?_ParentTypeName@TableDescription@UI@@CAPEBDXZ @0x180078ea0 -> "ElementDescription"
//
// RULE B5-15 / B6-7 — THE TWO NAME LITERALS, RECORDED, NOT USED (slots 1/2 keep
// reading the minted `?_typeInfo@TableDescription@UI@@…`, 0x1801fae40):
//     ?_TypeName@TableDescription@UI@@CAPEBDXZ       @0x180078e90 -> "TableDescription"
//     ?_ParentTypeName@TableDescription@UI@@CAPEBDXZ @0x180078ea0 -> "ElementDescription"
//
// `??_7TableDescription@UI@@6B@` @ 0x1801592c0, TEN slots (B36_SLOTS.md
// section 1; sizeof from `MOV EDX,0x98` in slot 0 0x180079210):
//   [0] 0x180079210 deleting dtor (row)  [1] 0x180078ed0 TypeName
//   [2] 0x180078eb0 Type                 [3] 0x18011e6e0 GetFileVersion
//   [4] 0x1800066e0 External::Compile INHERITED
//   [5] 0x18011e590 Serialize            [6] 0x180079050 Reconstruct
//   [7] 0x180079080 Destruct             [8] 0x18011e350 CreateInstance RETSLOT
//   [9] 0x180079070 Construct            INTRODUCED by the leaf (RULE B6-1.1)
//
// ENDGAME B.3 step 9 (block UI-B, B39_RULES D1/D2) -- the paragraph below is
// HISTORY: the class is now CONCRETE. A real public ctor (retail's stores), its
// own slot 0 (the vector deleting dtor), a virtual ~TableDescription holding
// the retired ??1 door's body, and the factory pair as the class's own statics;
// every live one wears the COMPILER's table. The minted `??_7TableDescription@UI@@6B@`,
// its RT_DELDTOR row 0x180079210 and the ctor / dtor / slot doors are retired
// (tools/rt_mint_retired/ui-b.txt).
// ABSTRACT (slot 0 pure), no real ctor: every live one is built by the
// `??0TableDescription` DOOR (tabledescription.cpp) and wears the MINTED table.
//
// LAYOUT (RULE B6-8) — MOVED here from tabledescription.cpp's raw offsets.
// Evidence: `??0TableDescription@UI@@QEAA@XZ` (0x18011e7a0), tail:
//     MOV dword [RCX+0x68],1 ; MOV qword [RCX+0x6c],1   (UNALIGNED: +0x6c = 1, +0x70 = 0)
//     MOV qword [RCX+0x78/0x80/0x88/0x90],0
// +0x74 is written by NOTHING (the unaligned qword stops at +0x73): an explicit
// pad. Serialize (0x18011e590): kScalarInt over +0x68/+0x6c/+0x70, then the
// +0x78 cell array (count, Array<Cell>::SetSize 0x1800799a0, per cell two
// kScalarInt and ExternalPtr<ElementDescription> 0x180038f90). +0x88 is written
// by the ctor and touched by nothing else in the class (`_unreadReserved`); +0x90 is a
// System::String only ~TableDescription (0x18011e6f0) releases (`_unreadOwnedString`).
// CreateInstance (0x18011e350) loops a over [0, +0x68) against cell.column and
// b over [0, +0x6c) against cell.row, and stores +0x68 to TableElement +0xc4
// (column count) and +0x6c to +0xc0 (row count): that is what names them.
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "ElementDescription.h"
#include "../collection/Collection.h"

namespace UI {

class Dialog;
class Element;

class TableDescription : public ElementDescription {
public:
    // One cell description: `Array<Cell>` stride 0x10 (0x1800799a0's spec). A
    // negative column or row is a wildcard (see CreateInstance's four-tier pick).
    struct Cell {
        int32_t column;          // +0x00  kScalarInt
        int32_t row;             // +0x04  kScalarInt
        ElementDescription* desc; // +0x08  ExternalPtr<UI::ElementDescription>
    };

    // ---- the ten slots, in measured order ----------------------------------
    // ??0TableDescription@UI@@QEAA@XZ 0x18011e7a0 -- the REAL ctor:
    // ElementDescription's stores (inlined in retail), then +0x68 = 1, ONE
    // UNALIGNED QWORD over +0x6c..+0x73 (_rowCount 1, _spacing 0), the cell
    // Array header (its own ctor), +0x88 and +0x90 zero. +0x74 is not written.
    TableDescription() : ElementDescription() {
        _columnCount = 1;
        _rowCount    = 1;
        _spacing     = 0;
        _unreadReserved   = 0;
        _unreadOwnedString   = nullptr;
    }

    // [0] 0x180079210 -- MSVC's VECTOR deleting destructor (retail; here the
    // virtual destructor, B3X W1) (`MOV EDX,0x98` = the element size).
    // `??1TableDescription@UI@@UEAA@XZ` (0x18011e6f0): VIRTUAL (the normal-virtual
    // cutover, B3X W1). Retail's body is in tabledescription.cpp;
    // the compiler chains the base's.
    ~TableDescription() override;
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x180078ed0, Type 0x180078eb0, _TypeName 0x180078e90, _ParentTypeName 0x180078ea0
    DECLARE_TYPE(TableDescription)
public:
    int32_t       GetFileVersion() const override;                // [3] 0x18011e6e0
    bool          Serialize(IO::Stream& stream) override;               // [5] 0x18011e590
    Object::External* Reconstruct() override;                     // [6] 0x180079050
    void          Destruct() override;                            // [7] 0x180079080
    Object::InternalPtr<Element>* CreateInstance(
        Object::InternalPtr<Element>* ret,
        const Dialog* dialog) const override;                     // [8] 0x18011e350 RETSLOT
    virtual Object::External* Construct();                        // [9] 0x180079070

    // ---- exported, not ported: declared, deliberately undefined ------------
    TableDescription(const TableDescription& src);                // 0x180079090
    TableDescription& operator=(const TableDescription& src);     // 0x180079130

private:

public:
    // ---- the 0x30 bytes after ElementDescription ---------------------------
    int32_t  _columnCount;        // +0x68  ctor 1; -> TableElement::_columnCount (+0xc4)
    int32_t  _rowCount;           // +0x6c  ctor 1; -> TableElement::_rowCount    (+0xc0)
    int32_t  _spacing;            // +0x70  ctor 0; -> TableElement::_spacing     (+0xc8)
    uint32_t _pad74;              // +0x74  written by nothing
    Collection::Array<Cell> _cells;   // +0x78 .. +0x87  ~Array 0x180079870
    uint64_t _unreadReserved;          // +0x88  ctor 0; no body of this class reads it
    char*    _unreadOwnedString;          // +0x90  System::String; only the dtor touches it
};

static_assert(sizeof(TableDescription::Cell) == 0x10, "Array<Cell> stride (0x1800799a0)");
static_assert(offsetof(TableDescription::Cell, column) == 0x0, "Cell::column");
static_assert(offsetof(TableDescription::Cell, row) == 0x4, "Cell::row");
static_assert(offsetof(TableDescription::Cell, desc) == 0x08, "");
static_assert(sizeof(TableDescription) == 0x98,
              "UI::TableDescription is 0x98 bytes (MOV EDX,0x98 in its slot-0 "
              "vector deleting destructor 0x180079210)");
static_assert(offsetof(TableDescription, _pad74) == 0x74, "TableDescription::_pad74");
static_assert(alignof(TableDescription) == 8, "UI::TableDescription aligns to 8");
static_assert(offsetof(TableDescription, _columnCount) == sizeof(ElementDescription), "");
static_assert(offsetof(TableDescription, _columnCount) == 0x68, "");
static_assert(offsetof(TableDescription, _rowCount)    == 0x6c, "");
static_assert(offsetof(TableDescription, _spacing)     == 0x70, "");
static_assert(offsetof(TableDescription, _cells)       == 0x78, "");
static_assert(offsetof(TableDescription, _unreadReserved)   == 0x88, "");
static_assert(offsetof(TableDescription, _unreadOwnedString)   == 0x90, "");

}  // namespace UI
