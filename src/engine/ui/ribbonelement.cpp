// ============================================================================
// src/engine/ui/ribbonelement.cpp — UI::RibbonElement, the whole class
// ============================================================================
//
// `??_7RibbonElement@UI@@6B@` (retail 0x180156e70):
//
//   0  vector deleting dtor  0x180044ac0  -> deleting_dtor.cpp row (this batch)
//   1  TypeName              0x1800446e0  here
//   2  Type                  0x1800446c0  here
//   4  ComputeSize           0x1800d6190  here
//   5  ComputeLayout         0x1800d6410  here
//   6  Draw                  0x1800d6740  here
//
// plus ??0RibbonElement (0x1800d5d10) and ??1RibbonElement (0x1800d5d70).
//
// NOT HERE: `?AddItem@RibbonElement@UI@@` (0x1800448e0), `?IsVertical@`
// (0x180044970) and `?GetCellLayout@` (0x1800d5db0) — not vtable slots, and
// AddItem is on the UI non-slot callee list.
//
// LAYOUT: src/engine/ui/RibbonElement.h (sizeof 0xD8; RULE B6-8 moved this
// file's offsets there under the retail name).
//
// ENDGAME B.3.6 block C5: the five slot bodies MOVED into the members of the
// real `UI::RibbonElement`; each registered door is a one-line QUALIFIED
// delegation. The ctor / dtor stay doors (B4b-2).
//
// UI::ListElement's +0xF0 points at one of these and reads exactly +0xC0,
// +0xC4 and +0xC8 through it — corroboration of all three offsets from a
// second class.
// ============================================================================

#include <new>

#include "widget_common.h"
#include "ui/RibbonElement.h"

using namespace rtui;

namespace {

constexpr int kVertical = 0xC0;
constexpr int kUniform  = 0xC1;
constexpr int kSpacing  = 0xC4;
constexpr int kExtents  = 0xC8;
static_assert(kVertical == offsetof(UI::RibbonElement, _vertical), "");
static_assert(kUniform  == offsetof(UI::RibbonElement, _uniform), "");
static_assert(kSpacing  == offsetof(UI::RibbonElement, _spacing), "");
static_assert(kExtents  == offsetof(UI::RibbonElement, _extentsData), "");

}  // namespace

namespace UI {

// ---------------------------------------------------------------------------
// [4] ?ComputeSize@RibbonElement@UI@@UEAAXXZ   0x1800d6190
// ---------------------------------------------------------------------------
void RibbonElement::ComputeSize() {
    if ((_flags & UI::kElementFlagNeedsSizeCalc) == 0) return;

    if (_extentsCount != _children.GetSize())
        rtcoll::RtColl_ArraySetSize_18002fea0(&_extentsData, _children.GetSize());

    int32_t maxW = 0, maxH = 0, sumW = 0, sumH = 0, visible = 0;

    const int32_t n = _children.GetSize();
    for (int32_t i = 0; i < n; ++i) {
        _extentsData[i] = 0;

        Element* child = _children.GetData()[i];
        if (child == nullptr || (child->_flags & UI::kElementFlagHidden) != 0) continue;

        int32_t w = 0, h = 0;
        InlineGetPaddedSize(child, &w, &h);

        if (maxH < h) maxH = h;
        if (maxW < w) maxW = w;
        sumW += w;
        sumH += h;

        _extentsData[i] = (_vertical != 0) ? h : w;

        if (w > 0 && h > 0) ++visible;
    }

    if (_uniform != 0) {
        sumH = _children.GetSize() * maxH;
        sumW = _children.GetSize() * maxW;
        const int32_t m = _extentsCount;
        for (int32_t i = 0; i < m; ++i)
            _extentsData[i] = (_vertical != 0) ? maxH : maxW;
    }

    int32_t gaps = visible - 1;
    if (gaps < 0) gaps = 0;
    const int32_t totalGap = _spacing * gaps;

    if (_vertical == 0) maxW = totalGap + sumW;
    _layoutWidth = maxW;
    if (_vertical != 0) maxH = totalGap + sumH;
    _flags &= ~UI::kElementFlagNeedsSizeCalc;
    _layoutHeight = maxH;
}

// ---------------------------------------------------------------------------
// [5] ?ComputeLayout@RibbonElement@UI@@UEAAXHHHH@Z   0x1800d6410
// ---------------------------------------------------------------------------
void RibbonElement::ComputeLayout(int32_t x, int32_t y, int32_t right, int32_t bottom) {
    if (_border != nullptr)
        _border->ComputeLayout(x, y, right, bottom);

    ComputeSize();   // slot 4, VIRTUAL — `CALL [RAX+0x20]` in retail

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

    _layoutLeft   = px;
    _layoutRight  = px + w;
    _layoutTop    = py;
    _layoutBottom = py + h;

    int32_t cursor = 0;
    const int32_t n = _children.GetSize();
    for (int32_t i = 0; i < n; ++i) {
        const int32_t extent = _extentsData[i];
        Element* child = _children.GetData()[i];
        if (child == nullptr || (child->_flags & UI::kElementFlagHidden) != 0) continue;

        int32_t cw = 0, ch = 0;
        InlineGetPaddedSize(child, &cw, &ch);

        child = _children.GetData()[i];
        const bool vertical = _vertical != 0;
        const bool last     = (i == _children.GetSize() - 1);

        if (!vertical) {
            int32_t end = _layoutLeft + extent + cursor;
            if ((_flags & UI::kElementFlagAlignArg1) != 0 && last)
                end = _layoutRight;
            child->ComputeLayout(_layoutLeft + cursor, _layoutTop, end, _layoutBottom);   // slot 5
        } else {
            int32_t end = _layoutTop + extent + cursor;
            if ((_flags & UI::kElementFlagAlignArg2) != 0 && last)
                end = _layoutBottom;
            child->ComputeLayout(_layoutLeft, _layoutTop + cursor, _layoutRight, end);    // slot 5
        }

        cursor += extent;
        if (cw > 0 && ch > 0) cursor += _spacing;
    }
}

// ---------------------------------------------------------------------------
// [6] ?Draw@RibbonElement@UI@@UEBAXAEAVSpriteContext@2@E@Z   0x1800d6740
// ---------------------------------------------------------------------------
void RibbonElement::Draw(SpriteContext& context, uint8_t alpha) const {
    if (_border != nullptr)
        _border->Draw(context, alpha);

    if ((_flags & UI::kElementFlagDimColor) != 0) alpha >>= 1;

    const int32_t n = _children.GetSize();
    for (int32_t i = 0; i < n; ++i) {
        const Element* child = _children.GetData()[i];
        if (child != nullptr && (child->_flags & UI::kElementFlagHidden) == 0)
            child->Draw(context, alpha);   // slot 6
    }
}

// ??1RibbonElement@UI@@UEAA@XZ   0x1800d5d70 — retail's body, moved from the
// retired door (ENDGAME B.3.9 UI-B): the extent Array through 0x18001c750. The
// restamp and the tail into ~Element are the compiler's.
RibbonElement::~RibbonElement() {
    rtcoll::RtColl_ArrayDtor_18001c750(&_extentsData);
}

// ?CreateNewObject@RibbonElement@UI@@SAPEAVBase@Object@@XZ 0x1800447d0 --
// `?MemAlloc@MemoryAllocator` (0x1800c7a50) of 0xd8, align 8; retail's null
// branch; the ctor. Replaces the factory interpreter row (B39_RULES D2).
Object::Base* RibbonElement::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(RibbonElement), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) RibbonElement();
}
// ?CreateInPlaceObject@RibbonElement@UI@@SAPEAVBase@Object@@PEAX@Z 0x180044880
Object::Base* RibbonElement::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) RibbonElement();
}

}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 UI-B: the slot doors, the ctor / dtor doors and the
// factory-interpreter row are gone. Their only readers were the minted
// `??_7RibbonElement@UI@@6B@`, its RT_DELDTOR row and FACTUI2: every live
// RibbonElement now wears the compiler's table.
// ============================================================================

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::RibbonElement's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@RibbonElement@UI@@ (0x1801fa2e0): name "RibbonElement", parent
// "Element" (retail ?_ParentTypeName@RibbonElement@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::RibbonElement, "Element", nullptr)
