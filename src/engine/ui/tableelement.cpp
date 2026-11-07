// ============================================================================
// src/engine/ui/tableelement.cpp — UI::TableElement, the whole class
// ============================================================================
//
// `??_7TableElement@UI@@6B@` (retail 0x180159260):
//
//   0  vector deleting dtor  0x180079600  -> deleting_dtor.cpp row (this batch)
//   1  TypeName              0x1800792d0  here
//   2  Type                  0x1800792b0  here
//   4  ComputeSize           0x18011df60  here
//   5  ComputeLayout         0x18011dd10  here
//   6  Draw                  0x18011dc20  here
//
// plus ??0TableElement (0x18011e2f0) and ??1TableElement (0x18011e2a0).
//
// NOT HERE: `?GetRowHeight@` (0x18011e090) and `?GetColumnWidth@`
// (0x18011e1a0) — both on the UI non-slot callee list; ComputeSize reaches
// them by address. `?SetChild@` / `?SetSize@` are lane E's, in
// widgets_layout.cpp.
//
// ---------------------------------------------------------------------------
// LAYOUT (src/engine/ui/TableElement.h, every offset already pinned there
// by the three exported mutators; the ctor at 0x18011e2f0 confirms them and
// adds the one it did not have):
//
//   +0xC0  _rowCount  int32   ?GetRowCount@    0x180079450
//   +0xC4  _colCount  int32   ?GetColumnCount@ 0x180079460
//   +0xC8  _spacing   int32   the gap inserted between two NON-EMPTY rows or
//                             columns — ctor 0, and the field TableElement.h
//                             did not name
//   +0xD0  _rowSizes  Collection::Array<int32>
//   +0xE0  _colSizes  Collection::Array<int32>
//
// sizeof == 0xF0 (deleting dtor `MOV EDX,0xf0`). ~TableElement destroys
// _colSizes FIRST and then _rowSizes — reverse declaration order, as C++
// requires — through the shared `~Array` copy at 0x18001c750.
//
// The CELLS are UI::Element::_children itself (+0xA0), indexed
// `_colCount * row + col`; there is no second array.
//
// ENDGAME B.3.6 block C4: the layout is the real class in src/engine/ui/
// TableElement.h. Every slot body below is a MEMBER; each registered door is a
// one-line QUALIFIED delegation; ctor/dtor stay DOORS. RULE B6-5: the vtable
// and type-info record are cached in namespace-scope std::atomic slots.
// ============================================================================

#include <new>

#include "widget_common.h"
#include "ui/TableElement.h"

using namespace rtui;

namespace {

// `?GetRowHeight@` (0x18011e090) / `?GetColumnWidth@` (0x18011e1a0) — declared
// on the class, bodies still element_bodies_b5.cpp's doors (fragment pass).

}  // namespace

// ============================================================================
// The member bodies (ENDGAME B.3.6 block C4)
// ============================================================================
namespace UI {

// ---------------------------------------------------------------------------
// slot 4 — ?ComputeSize@TableElement@UI@@UEAAXXZ   0x18011df60
//
// Total = sum of extents + one spacing between each pair of NON-EMPTY ones.
// The "first non-empty" flag is what keeps a leading zero-width column from
// contributing a gap.
// ---------------------------------------------------------------------------
void TableElement::ComputeSize() {
    if ((_flags & UI::kElementFlagNeedsSizeCalc) == 0) return;

    _layoutWidth  = 0;     // one qword store
    _layoutHeight = 0;

    bool first = true;
    for (int32_t c = 0; c < _columnCount; ++c) {
        int32_t* cols = _columnSizes._data;                                 // loaded BEFORE the call
        cols[c] = GetColumnWidth(c);                                        // 0x18011e1a0
        cols = _columnSizes._data;                                          // re-read
        _layoutWidth = _layoutWidth + cols[c];
        if (cols[c] != 0) {
            if (first) first = false;
            else _layoutWidth = _spacing + _layoutWidth;
        }
    }

    first = true;
    for (int32_t r = 0; r < _rowCount; ++r) {
        int32_t* rows = _rowSizes._data;                                    // loaded BEFORE the call
        rows[r] = GetRowHeight(r);                                          // 0x18011e090
        rows = _rowSizes._data;                                             // re-read
        _layoutHeight = rows[r] + _layoutHeight;
        if (rows[r] != 0) {
            if (first) first = false;
            else _layoutHeight = _spacing + _layoutHeight;
        }
    }

    _flags &= ~UI::kElementFlagNeedsSizeCalc;
}

// ---------------------------------------------------------------------------
// slot 5 — ?ComputeLayout@TableElement@UI@@UEAAXHHHH@Z   0x18011dd10
// ---------------------------------------------------------------------------
void TableElement::ComputeLayout(int32_t x, int32_t y, int32_t right, int32_t bottom) {
    if (Element* border = _border)
        border->ComputeLayout(x, y, right, bottom);

    ComputeSize();                                            // slot 4, virtual

    int32_t w = _layoutWidth;
    int32_t h = _layoutHeight;
    const int32_t align = _alignment;

    if (right  < w + x) x = right  - w;
    if (bottom < h + y) y = bottom - h;

    int32_t px = x + _leftPad;
    int32_t py = y + _topPad;
    const int32_t aw = (right  - _rightPad)  - px;
    const int32_t ah = (bottom - _bottomPad) - py;

    if ((_flags & UI::kElementFlagAlignArg1) != 0) w = aw;
    if ((_flags & UI::kElementFlagAlignArg2) != 0) h = ah;

    switch (align) {
        case ElementDescription::TopCenter:
        case ElementDescription::MidCenter:
        case ElementDescription::BottomCenter:
            px = ((aw >> 1) - (w >> 1)) + px;
            break;
        case ElementDescription::TopRight:
        case ElementDescription::MidRight:
        case ElementDescription::BottomRight:
            px = (right - _rightPad) - w;
            break;
        default: break;
    }
    if (align > ElementDescription::TopRight) {
        if (align < ElementDescription::BottomLeft)
            py = ((ah >> 1) - (h >> 1)) + py;
        else if (align <= ElementDescription::BottomRight)
            py = (bottom - _bottomPad) - h;
    }

    _layoutRight  = px + w;
    _layoutLeft   = px;
    _layoutTop    = py;
    _layoutBottom = py + h;

    int32_t cellTop = py;
    for (int32_t r = 0; r < _rowCount; ++r) {
        const int32_t rowH = _rowSizes._data[r];
        int32_t cellLeft = px;
        for (int32_t c = 0; c < _columnCount; ++c) {
            const int32_t colW = _columnSizes._data[c];
            Element* cell = _children[_columnCount * r + c];
            if (cell != nullptr && (cell->_flags & UI::kElementFlagHidden) == 0)
                cell->ComputeLayout(cellLeft, cellTop, colW + cellLeft, cellTop + rowH);
            cellLeft += colW;
            if (colW > 0) cellLeft += _spacing;
        }
        cellTop += rowH;
        if (rowH > 0) cellTop += _spacing;
    }
}

// ---------------------------------------------------------------------------
// slot 6 — ?Draw@TableElement@UI@@UEBAXAEAVSpriteContext@2@E@Z   0x18011dc20
// ---------------------------------------------------------------------------
void TableElement::Draw(SpriteContext& context, uint8_t alpha) const {
    if (Element* border = _border)
        border->Draw(context, alpha);

    if ((_flags & UI::kElementFlagDimColor) != 0) alpha >>= 1;

    for (int32_t r = 0; r < _rowCount; ++r) {
        int32_t cols = _columnCount;
        for (int32_t c = 0; c < cols; ++c) {
            Element* cell = _children[cols * r + c];
            if (cell != nullptr && (cell->_flags & UI::kElementFlagHidden) == 0)
                cell->Draw(context, alpha);
            cols = _columnCount;
        }
    }
}

// ??1TableElement@UI@@UEAA@XZ   0x18011e2a0 — retail's body, moved from the
// retired door (ENDGAME B.3.9 UI-B): the two extent Arrays through 0x18001c750,
// columns first (each frees and zeroes its header, so the compiler's member
// ~Arrays afterwards are no-ops). The restamp and the tail into ~Element are
// the compiler's.
TableElement::~TableElement() {
    rtcoll::RtColl_ArrayDtor_18001c750(&_columnSizes);
    rtcoll::RtColl_ArrayDtor_18001c750(&_rowSizes);
}

// ?CreateNewObject@TableElement@UI@@SAPEAVBase@Object@@XZ 0x1800793c0 --
// `?MemAlloc@MemoryAllocator` (0x1800c7a50) of 0xf0, align 8; retail's null
// branch; the ctor. Replaces the factory interpreter row (B39_RULES D2).
Object::Base* TableElement::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TableElement), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) TableElement();
}
// ?CreateInPlaceObject@TableElement@UI@@SAPEAVBase@Object@@PEAX@Z 0x180079440
Object::Base* TableElement::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) TableElement();
}

}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 UI-B: the slot doors, the ctor / dtor doors and the
// factory-interpreter row are gone. Their only readers were the minted
// `??_7TableElement@UI@@6B@`, its RT_DELDTOR row and FACTUI2: every live
// TableElement now wears the compiler's table.
// ============================================================================

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::TableElement's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@TableElement@UI@@ (0x1801fae78): name "TableElement", parent
// "Element" (retail ?_ParentTypeName@TableElement@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::TableElement, "Element", nullptr)
