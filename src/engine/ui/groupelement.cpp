// ============================================================================
// src/engine/ui/groupelement.cpp — UI::GroupElement, the whole class
// ============================================================================
//
// 2026-09-12, vtable batch 3. `??_7GroupElement@UI@@6B@` (retail 0x180157130),
// the plain stacking container: no fields of its own, sized to the union of its
// visible children, and every child given the WHOLE group rectangle.
//
//   slot  method                retail       where
//   ----  --------------------  -----------  ---------------------------------
//    0    vector deleting dtor  0x180046e10  GroupElement.h (the class's own)
//    1    TypeName              0x180046bb0  here
//    2    Type                  0x180046b90  here
//    4    ComputeSize           0x180117f00  here
//    5    ComputeLayout         0x180118050  here
//    6    Draw                  0x1801181c0  here
//
// plus ??1GroupElement@UI@@UEAA@XZ 0x180117cc0 and the factory pair (ENDGAME
// B.3.9 UI-A: the real ctor ??0 0x180117c80 is in the header; the copy-ctor
// door is retired).
//
// Slots 4/5/6 are ALSO PageElement's and RadioElement's — both repeat these
// three addresses in their own tables. Registered once, here; in C++ the two
// subclasses simply do not override them.
//
// LAYOUT: src/engine/ui/GroupElement.h — 0xc0 bytes, exactly
// `sizeof(UI::Element)`.
//
// ENDGAME B.3.6 block C2: every slot body is a MEMBER of `UI::GroupElement`;
// the registered doors are one-line QUALIFIED delegations. The raw slot reads
// on the border and the children (`VSlot<…>(p, kSlot…)`) are VIRTUAL member
// calls now — they must dispatch, since each child is its own class.
//
// ENDGAME B.3.9 block UI-A: the class is CONCRETE; the slot doors (4/5/6 were
// also Page's and Radio's) and the three ctor/dtor doors, read only by the
// retired minted tables and RT_DELDTOR rows, are gone.
// ============================================================================
#include "element_common.h"
#include "ui/GroupElement.h"

using namespace rtuielem;

namespace {

}  // namespace

namespace UI {

// ============================================================================
// ?ComputeSize@GroupElement@UI@@UEAAXXZ   0x180117f00
// ============================================================================
// The union of every VISIBLE child's padded size. Hidden children (flag bit 0)
// contribute nothing — this is the one sizing loop in the batch that tests the
// flag, and it is why hiding a child in a group actually shrinks the group.
void GroupElement::ComputeSize() {
    if ((_flags & kElementFlagNeedsSizeCalc) == 0) return;

    // +0x60 and +0x64 zeroed as ONE qword store (H7: two named stores).
    _layoutWidth  = 0;
    _layoutHeight = 0;

    const int32_t count = _children._size;
    for (int32_t i = 0; i < count; ++i) {
        Element* child = _children._data[i];
        if (child == nullptr) continue;
        if ((child->_flags & kElementFlagHidden) != 0) continue;

        int w = 0, h = 0;
        PaddedSizeInline(child, &w, &h);
        if (_layoutWidth  < w) _layoutWidth  = w;
        if (_layoutHeight < h) _layoutHeight = h;
    }

    _flags &= ~kElementFlagNeedsSizeCalc;
}

// ============================================================================
// ?ComputeLayout@GroupElement@UI@@UEAAXHHHH@Z   0x180118050
// ============================================================================
// *** THIS IS *NOT* THE STANDARD ALIGNED BODY *** and must not be folded into
// `ElementComputeAlignedRect`. Three differences, all at 0x1801180a0..0x180118120:
//
//   * flag bits 2 and 3 are NOT consulted; the group ALWAYS takes the available
//     size, so `sw`/`sh` below are the available width/height unconditionally;
//   * there is consequently no `case 1/4/7` centring arm on the X axis — the
//     jump table has only the right-aligned cases;
//   * the Y test is `align > 2 && align > 5 && align < 9`, i.e. only the BOTTOM
//     row, with no middle-row arm.
//
// The border and the child calls are VIRTUAL (slot 5 of THEIR tables).
void GroupElement::ComputeLayout(int32_t x, int32_t y, int32_t w, int32_t h) {
    Element* border = _border;
    if (border != nullptr)
        border->ComputeLayout(x, y, w, h);

    ComputeSize();                                  // virtual — slot 4 of THIS object's table

    const int align = _alignment;

    if (w < _layoutWidth  + x) x = w - _layoutWidth;
    if (h < _layoutHeight + y) y = h - _layoutHeight;

    int cx = x + _leftPad;
    int cy = y + _topPad;
    const int availW = (w - _rightPad)  - cx;
    const int availH = (h - _bottomPad) - cy;

    switch (align) {
    case ElementDescription::TopRight:
    case ElementDescription::MidRight:
    case ElementDescription::BottomRight:
        cx = (w - _rightPad) - availW;
        break;
    default: break;
    }
    if (align > ElementDescription::TopRight && align > ElementDescription::MidRight &&
        align <= ElementDescription::BottomRight)
        cy = (h - _bottomPad) - availH;

    _layoutLeft   = cx;
    _layoutRight  = availW + cx;
    _layoutTop    = cy;
    _layoutBottom = availH + cy;

    const int32_t count = _children._size;
    for (int32_t i = 0; i < count; ++i) {
        Element* child = _children._data[i];
        if (child == nullptr) continue;
        if ((child->_flags & kElementFlagHidden) != 0) continue;
        child->ComputeLayout(_layoutLeft, _layoutTop, _layoutRight, _layoutBottom);
    }
}

// ============================================================================
// ?Draw@GroupElement@UI@@UEBAXAEAVSpriteContext@2@E@Z   0x1801181c0
// ============================================================================
void GroupElement::Draw(SpriteContext& context, uint8_t alpha) const {
    const Element* border = _border;
    if (border != nullptr)
        border->Draw(context, alpha);

    alpha = DimAlpha(this, alpha);

    const int32_t count = _children._size;
    for (int32_t i = 0; i < count; ++i) {
        const Element* child = _children._data[i];
        if (child == nullptr) continue;
        if ((child->_flags & kElementFlagHidden) != 0) continue;
        child->Draw(context, alpha);
    }
}

}  // namespace UI

// ============================================================================
// ??1GroupElement@UI@@UEAA@XZ   0x180117cc0 -- restamp, then ~Element.
// ENDGAME B.3.9 UI-A: EMPTY; the compiler restamps and chains ~Element.
// ============================================================================
namespace UI {
GroupElement::~GroupElement() {}
}  // namespace UI

namespace UI {
// ?CreateNewObject@GroupElement@UI@@SAPEAVBase@Object@@XZ  0x180046ca0
// The factory-interpreter row this replaces: MemAlloc(0xc0, 8), retail's
// null branch, then ??0Element and the stamp.
Object::Base* GroupElement::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(GroupElement), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) GroupElement();
}
// ?CreateInPlaceObject@GroupElement@UI@@SAPEAVBase@Object@@PEAX@Z  0x180046d30
Object::Base* GroupElement::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) GroupElement();
}
}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::GroupElement's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@GroupElement@UI@@ (0x1801fabd8): name "GroupElement", parent
// "Element" (retail ?_ParentTypeName@GroupElement@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::GroupElement, "Element", nullptr)
