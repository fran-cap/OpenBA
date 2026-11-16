#pragma once
// UI::TableElement -- offsets used by Wave 7 lane E, proven against retail
// Runtime-x64.dll disassembly (docs/history/runtime_port/WAVE7_LANE_E.md has the
// per-offset call sites).
//
// Element.h (lane A) landed while this lane's recon was in progress and
// CONFIRMS the inference this header originally had to make independently:
// sizeof(UI::Element) == 0xC0, Element::_flags @ +0x34, Element::_parent
// @ +0x98, and Element::_children (Collection::Array<Element*>) @ +0xA0 --
// exactly the fields TableElement::SetChild/SetSize touch below. The shared
// fields are now expressed via offsetof(UI::Element, ...) rather than a
// second copy of the magic numbers; only TableElement's OWN new fields
// (+0xC0 and up) are this header's own literals.
//
// Evidence (Runtime-x64.dll addresses):
//   GetRowCount     0x180079450  return *(int*)(this+0xc0)
//   GetColumnCount  0x180079460  return *(int*)(this+0xc4)
//   SetChild(e,col) 0x18011daa0  this+0xa0 = Collection::Array<InternalPtr<Element>>
//                                 head (data ptr at this+0xa0, indexed *8);
//                                 dirty chain via this+0x34 / this+0x98
//   SetChild(e,c,r) 0x18011db50  index = GetColumnCount()*col + row, delegates
//                                 to the two-arg SetChild
//   SetSize(r,c)    0x18011db90  this+0xc0=r, this+0xc4=c; resizes the two
//                                 int32 Arrays at +0xd0/+0xe0 (row/col extents)
//                                 and the cell Array at +0xa0 to r*c cells

#include <cstdint>
#include <cstddef>
#include "Element.h"

namespace UI { namespace TableElementOffsets {

constexpr int kCells      = offsetof(UI::Element, _children); // 0xA0 -- Element's OWN children array; TableElement's cell grid IS this array
constexpr int kRowCount   = 0xC0;  // int32_t -- TableElement's own field
constexpr int kColCount   = 0xC4;  // int32_t -- TableElement's own field
constexpr int kRowSizes   = 0xD0;  // Collection::Array<int32_t>, 0x10 bytes -- TableElement's own field
constexpr int kColSizes   = 0xE0;  // Collection::Array<int32_t>, 0x10 bytes -- TableElement's own field
constexpr int kDirtyFlags = offsetof(UI::Element, _flags);  // 0x34
constexpr int kParent     = offsetof(UI::Element, _parent); // 0x98

// UI::Element's kElementFlagNeedsSizeCalc (Element.h), OR'd into kDirtyFlags
// by every mutator in this lane and walked up the kParent chain.
constexpr uint32_t kDirtyBit = UI::kElementFlagNeedsSizeCalc;

} } // namespace UI::TableElementOffsets

// ############################################################################
// ENDGAME B.3 step 6 (block C4) — `UI::TableElement`, the REAL C++ class
// ############################################################################
//
// PARENT `UI::Element` — READ, not inferred:
//     ?_ParentTypeName@TableElement@UI@@CAPEBDXZ @0x1800792a0 -> "Element"
//
// RULE B5-15 / B6-7 — THE TWO NAME LITERALS, RECORDED (the class's own record is
// DEFINE_TYPE in tableelement.cpp):
//     ?_TypeName@TableElement@UI@@CAPEBDXZ       @0x180079290 -> "TableElement"
//     ?_ParentTypeName@TableElement@UI@@CAPEBDXZ @0x1800792a0 -> "Element"
//
// `??_7TableElement@UI@@6B@` @ 0x180159260, TWELVE slots (B36_SLOTS.md
// section 1; sizeof from `MOV EDX,0xf0` in slot 0 0x180079600):
//   [0] 0x180079600 deleting dtor        [1] 0x1800792d0 TypeName
//   [2] 0x1800792b0 Type                 [3] Element::Persist INHERITED
//   [4] 0x18011df60 ComputeSize          [5] 0x18011dd10 ComputeLayout
//   [6] 0x18011dc20 Draw                 [7] Element::Update INHERITED
//   [8]..[11] Element's, INHERITED
//
// ENDGAME B.3 step 9 (block UI-B, B39_RULES D1/D2) -- the paragraph below is
// HISTORY: the class is now CONCRETE. A real public ctor (retail's stores), its
// own slot 0 (the vector deleting dtor), a virtual ~TableElement holding
// the retired ??1 door's body, and the factory pair as the class's own statics;
// every live one wears the COMPILER's table. The minted `??_7TableElement@UI@@6B@`,
// its RT_DELDTOR row 0x180079600 and the ctor / dtor / slot doors are retired.
// ABSTRACT (slot 0 pure), no real ctor: every live one is built by the
// `??0TableElement` DOOR (tableelement.cpp) and wears the MINTED table.
//
// LAYOUT (RULE B6-8) — the offsets above, now named; `??0TableElement@UI@@
// QEAA@XZ` (0x18011e2f0), read as disassembly, adds +0xc8:
//     MOV qword [RBX+0xc0],0 ; MOV dword [RBX+0xc8],0
//     MOV qword [RBX+0xd0/0xd8/0xe0/0xe8],0
// +0xcc is written by nothing (explicit pad). TableDescription::CreateInstance
// (0x18011e350) copies the description's column count to +0xc4 and its row
// count to +0xc0 — crossed in memory order — and its +0x70 to +0xc8, the gap
// ComputeSize/ComputeLayout insert between two non-empty rows or columns.
// ~TableElement (0x18011e2a0) destroys _columnSizes first, then _rowSizes.
// ############################################################################
namespace UI {

class TableElement : public Element {
public:
    // ---- the twelve slots, in measured order -------------------------------
    // ??0TableElement@UI@@QEAA@XZ 0x18011e2f0 -- the REAL ctor: `CALL
    // ??0Element`, +0xc0/+0xc4 (one qword) and +0xc8 zero, and the two Array
    // headers (their own ctors). +0xcc is written by nothing.
    TableElement() : Element() {
        _rowCount    = 0;
        _columnCount = 0;
        _spacing     = 0;
    }

    // [0] 0x180079600 -- MSVC's VECTOR deleting destructor (retail; here the
    // virtual destructor, B3X W1) (`MOV EDX,0xf0` = the element size).
    // `??1TableElement@UI@@UEAA@XZ` (0x18011e2a0): VIRTUAL (the normal-virtual
    // cutover, B3X W1). Retail's body is in tableelement.cpp;
    // the compiler chains the base's.
    ~TableElement() override;
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x1800792d0, Type 0x1800792b0, _TypeName 0x180079290, _ParentTypeName 0x1800792a0
    DECLARE_TYPE(TableElement)
public:
    void ComputeSize() override;                                  // [4] 0x18011df60
    void ComputeLayout(int32_t x, int32_t y,
                       int32_t right, int32_t bottom) override;   // [5] 0x18011dd10
    void Draw(SpriteContext& context, uint8_t alpha) const override; // [6] 0x18011dc20
    // [7] Update and [8]..[11] — Element's, inherited.

    // ---- non-virtual members -----------------------------------------------
    int32_t GetRowCount() const    { return _rowCount; }          // 0x180079450 MOV EAX,[RCX+0xc0]
    int32_t GetColumnCount() const { return _columnCount; }       // 0x180079460 MOV EAX,[RCX+0xc4]
    // PORTED; DEFINED in the files named on each line (fragment pass). Retail
    // passes the by-value InternalPtr as
    // the ADDRESS of the caller's temporary, so it is spelled as that pointer.
    int32_t GetRowHeight(int32_t row);                            // 0x18011e090  element_bodies_b5.cpp
    int32_t GetColumnWidth(int32_t column);                       // 0x18011e1a0  element_bodies_b5.cpp
    void SetChild(Object::InternalPtr<Element>* child, int32_t index);          // 0x18011daa0  widgets_layout.cpp
    void SetChild(Object::InternalPtr<Element>* child, int32_t column,
                  int32_t row);                                   // 0x18011db50  widgets_layout.cpp
    void SetSize(int32_t rows, int32_t columns);                  // 0x18011db90  widgets_layout.cpp

    TableElement(const TableElement& src);                        // 0x180079470  not ported
    TableElement& operator=(const TableElement& src);             // 0x180079520  not ported

private:

public:
    // ---- the 0x30 bytes after Element --------------------------------------
    // The CELLS are Element::_children itself, row-major, `_columnCount * row + col`.
    int32_t  _rowCount;          // +0xc0  ctor 0 (one qword with +0xc4); GetRowCount
    int32_t  _columnCount;       // +0xc4  ctor 0; GetColumnCount
    int32_t  _spacing;           // +0xc8  ctor 0; gap between two non-empty rows/columns
    uint32_t _padcc;             // +0xcc  written by nothing
    Collection::Array<int32_t> _rowSizes;      // +0xd0  per-row extent, ComputeSize
    Collection::Array<int32_t> _columnSizes;   // +0xe0  per-column extent, ComputeSize
};

static_assert(sizeof(TableElement) == 0xf0,
              "UI::TableElement is 0xf0 bytes (MOV EDX,0xf0 in its slot-0 "
              "vector deleting destructor 0x180079600)");
static_assert(offsetof(TableElement, _padcc) == 0xCC, "TableElement::_padcc");
static_assert(alignof(TableElement) == 8, "UI::TableElement aligns to 8");
static_assert(offsetof(TableElement, _rowCount) == sizeof(Element), "");
static_assert(offsetof(TableElement, _rowCount)    == TableElementOffsets::kRowCount, "");
static_assert(offsetof(TableElement, _columnCount) == TableElementOffsets::kColCount, "");
static_assert(offsetof(TableElement, _spacing)     == 0xc8, "");
static_assert(offsetof(TableElement, _rowSizes)    == TableElementOffsets::kRowSizes, "");
static_assert(offsetof(TableElement, _columnSizes) == TableElementOffsets::kColSizes, "");
static_assert(offsetof(TableElement, _children)    == TableElementOffsets::kCells, "");

}  // namespace UI
