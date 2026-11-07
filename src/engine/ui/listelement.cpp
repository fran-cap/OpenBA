// ============================================================================
// src/engine/ui/listelement.cpp — UI::ListElement, the whole class
// ============================================================================
//
// `??_7ListElement@UI@@6B@` (retail 0x180156dc0):
//
//   0  vector deleting dtor  0x180045320  -> deleting_dtor.cpp row (this batch)
//   1  TypeName              0x180044f90  here
//   2  Type                  0x180044f70  here
//   4  ComputeSize           0x1800d4380  here
//   5  ComputeLayout         0x1800d4210  here
//   6  Draw                  0x1800d3eb0  here
//   7  Update                0x1800d3c20  here
//
// plus ??0ListElement (0x1800d4570) and ??1ListElement (0x1800d4530).
//
// NOT HERE: `?DrawHighlight@ListElement@UI@@QEBAXAEAVSpriteContext@2@HI@Z`
// (0x1800d3f70), `?AddItem@` (0x1800d44b0) and `?GetCellLayout@` (0x1800d4120)
// — the UI non-slot callee list is another agent's. Draw REACHES DrawHighlight
// by address.
//
// ---------------------------------------------------------------------------
// LAYOUT.  sizeof(ListElement) == 0x100 (the deleting destructor's
// `MOV EDX,0x100` at 0x180045352). Fields, each from the ctor (0x1800d4570) or
// from the body that reads it:
//
//   +0xC0  _itemRows      Array-ish head, ctor zeroes the qword
//   +0xC8  _rowCount      int32, ctor -1
//   +0xD0  _hoverColor    uint32, ctor 0xff808080   } one qword store,
//   +0xD4  _selectColor   uint32, ctor 0xffffffff   } 0xffffffffff808080
//   +0xD8  _selectedIndex int32, ctor -1            } one qword store
//   +0xDC  _hoverIndex    int32, ctor -1            }
//   +0xE0  _items         Array<...>, the ONE member ~ListElement destroys
//   +0xF0  _grid          UI::RibbonElement*  (Update reads its +0xC0 vertical
//                         byte, +0xC4 spacing and +0xC8 int array — exactly
//                         RibbonElement's own three fields, which is how the
//                         type is known and not guessed)
//   +0xF8  _clip          UI::ClipRegionElement*, the visible window
//
// Update's own two children are _children[0] (drawn first, the backdrop) and
// _children[1] (the content, and the one ComputeSize measures).
//
// ENDGAME B.3.6 block C4: the layout above moved to src/engine/ui/
// ListElement.h (the real class). Every slot body below is a MEMBER; each
// registered door is a one-line QUALIFIED delegation; ctor/dtor stay DOORS.
// The ribbon's own fields (UI::RibbonElement, block C5) and the dialog's slot
// 8 (UI::DialogInstance, block B) stay raw here — fragment list.
// RULE B6-5: the vtable and type-info record are cached in namespace-scope
// std::atomic slots.
// ============================================================================

#include <new>

#include "widget_common.h"
#include "ui/ListElement.h"
#include "ui/DialogInstance.h"   // B3X W0: OnElementEvent by name
#include "ui/RibbonElement.h"    // B4: _ribbon's three fields by name

using namespace rtui;

namespace {

// `?DrawHighlight@ListElement@UI@@QEBAXAEAVSpriteContext@2@HI@Z` 0x1800d3f70 —
// declared on the class, body still element_bodies_b5.cpp's door.

// `?OnElementEvent@DialogInstance@UI@@` — the DialogInstance vtable's slot 8
// (+0x40), called by name in Update. Seven arguments after `this`, read
// straight off 0x1800d3e2a..3e5e: RDX = &InternalPtr<Element>, R8D = event
// kind (0x14), R9D = the element id, [RSP+0x20] = the row index, and three
// zeroed dwords/qwords.

}  // namespace

// ============================================================================
// The member bodies (ENDGAME B.3.6 block C4)
// ============================================================================
namespace UI {

// slot 4 — 0x1800d4380
void ListElement::ComputeSize() {
    if ((_flags & UI::kElementFlagNeedsSizeCalc) == 0) return;
    Element* content = _children[1];
    content->GetPaddedSize(_layoutWidth, _layoutHeight);             // 0x180115f10
    _flags &= ~UI::kElementFlagNeedsSizeCalc;
}

// slot 5 — 0x1800d4210
void ListElement::ComputeLayout(int32_t x, int32_t y, int32_t right, int32_t bottom) {
    if (Element* border = _border)
        border->ComputeLayout(x, y, right, bottom);

    ComputeSize();                                            // slot 4, virtual

    const int32_t align = _alignment;
    if (right  < _layoutWidth  + x) x = right  - _layoutWidth;
    if (bottom < _layoutHeight + y) y = bottom - _layoutHeight;

    int32_t px = x + _leftPad;
    int32_t py = y + _topPad;
    const int32_t aw = (right  - _rightPad)  - px;
    const int32_t ah = (bottom - _bottomPad) - py;

    switch (align) {
        case ElementDescription::TopRight:
        case ElementDescription::MidRight:
        case ElementDescription::BottomRight:
            px = (right - _rightPad) - aw;
            break;
        default:
            break;
    }
    if (align > ElementDescription::TopRight && align > ElementDescription::MidRight &&
        align <= ElementDescription::BottomRight)
        py = (bottom - _bottomPad) - ah;

    _layoutLeft   = px;
    _layoutRight  = aw + px;
    _layoutTop    = py;
    _layoutBottom = ah + py;

    int32_t n = _children.GetSize();
    for (int32_t i = 0; i < n; ++i) {
        Element* child = _children[i];
        if (child != nullptr && (child->_flags & UI::kElementFlagHidden) == 0)
            child->ComputeLayout(_layoutLeft, _layoutTop, _layoutRight, _layoutBottom);
        n = _children.GetSize();
    }
}

// slot 6 — 0x1800d3eb0
void ListElement::Draw(SpriteContext& context, uint8_t alpha) const {
    if (Element* border = _border)
        border->Draw(context, alpha);

    Element* back = _children[0];
    if (back != nullptr && (back->_flags & UI::kElementFlagHidden) == 0)
        back->Draw(context, alpha);

    DrawHighlight(context, _hoverIndex, _hoverColor);                 // 0x1800d3f70
    DrawHighlight(context, _selectedIndex, _selectColor);

    Element* content = _children[1];
    if (content != nullptr && (content->_flags & UI::kElementFlagHidden) == 0)
        content->Draw(context, alpha);
}

// ---------------------------------------------------------------------------
// slot 7 — ?Update@ListElement@UI@@ ...   0x1800d3c20
//
// ONE stack slot holds two different things in sequence, and that is retail's
// own reuse, not a simplification: [RSP+0x70] first holds the caller's
// InternalPtr<DialogInstance> copy that Element::Update is handed (0x1800d3c40),
// and later `this` as an InternalPtr<Element> for the event (0x1800d3e37).
// ---------------------------------------------------------------------------
void ListElement::Update(const System::Time& time, UserInterface* ui,
                         Object::InternalPtr<DialogInstance>* dialog,
                         const UpdateState& state) {
    Object::InternalPtr<DialogInstance> slot(dialog->Get());   // 1800d3c2a / 3c40
    UI::Element::Update(time, ui, &slot, state);               // 1800d3c4a (qualified)

    _hoverIndex = -1;                                          // 1800d3c56
    if ((_flags & UI::kElementFlagHovered) == 0) return;       // TEST ...,0x20000

    const int32_t mouseX = state.mouseX;
    const int32_t mouseY = state.mouseY;

    for (int32_t i = 0; i < _ribbon->_children.GetSize(); ++i) {
        if (i < 0) continue;                                   // TEST EBX,EBX / JS

        // The RibbonElement ListDescription::CreateInstance found by its type
        // test (ListElement.h): _vertical +0xC0, _spacing +0xC4, _extentsData +0xC8.
        const RibbonElement* grid = static_cast<const RibbonElement*>(_ribbon);

        // The running offset of row i: the sum of the previous rows' extents
        // plus one spacing each. Retail unrolls this by four; the rolled form
        // computes the same sum in the same order.
        int32_t off = 0;
        for (int32_t k = 0; k < i; ++k)
            off += grid->_extentsData[k]
                   + grid->_spacing;

        int32_t x0 = grid->_layoutLeft;
        int32_t y0 = grid->_layoutTop;
        int32_t x1, y1;
        if (grid->_vertical == 0) {
            y1 = grid->_layoutBottom;
            x0 = x0 + off;
            x1 = grid->_extentsData[i] + x0;
        } else {
            x1 = grid->_layoutRight;
            y0 = y0 + off;
            y1 = grid->_extentsData[i] + y0;
        }

        Element* clip = _clip;
        int32_t cl = clip->_layoutLeft;
        int32_t cr = clip->_layoutRight;
        int32_t ct = clip->_layoutTop;
        int32_t cb = clip->_layoutBottom;

        if (cl <= x1 && x0 <= cr && ct <= y1 && y0 <= cb) {
            if (cb < y1) y1 = cb + 1;
            if (y0 < ct) y0 = ct;
            if (cr < x1) x1 = cr + 1;
            if (x0 < cl) x0 = cl;
            if (grid->_vertical != 0) {
                // A vertical list hit-tests the full clip width and the
                // clamped row band; a horizontal one the clamped row band and
                // the full clip height. Retail expresses that by overwriting
                // four registers here rather than by two separate tests.
                x1 = cr;
                cb = y1;
                ct = y0;
                x0 = cl;
            }
            if (x0 <= mouseX && mouseX <= x1 && ct <= mouseY && mouseY <= cb) {
                _hoverIndex = i;
                if (state.buttonPressed != 0) {
                    _selectedIndex = i;
                    // Retail reuses the [RSP+0x70] slot for `this`; nothing
                    // reads the earlier value again, so it is its own local.
                    Object::InternalPtr<Element> elemSlot(this);   // 1800d3e37
                    DialogInstance* dlg = dialog->Get();           // 1800d3e2a
                    // UI::DialogInstance slot 8 (block B), by name.
                    dlg->OnElementEvent(&elemSlot, EventId::SelectionChanged, _elementId, i, 0, 0, nullptr);
                }
            }
        }
    }
}

// ??1ListElement@UI@@UEAA@XZ   0x1800d4530 — retail's body, moved from the
// retired door (ENDGAME B.3.9 UI-B): the user-data Array through 0x180024aa0
// (frees and zeroes the header; the compiler's `_userData.~Array()` is then a
// no-op). The restamp and the tail into ~Element are the compiler's.
ListElement::~ListElement() {
    rtcoll::RtColl_ArrayDtor_180024aa0(&_userData);
}

// ?CreateNewObject@ListElement@UI@@SAPEAVBase@Object@@XZ 0x180045080 --
// `?MemAlloc@MemoryAllocator` (0x1800c7a50) of 0x100, align 8; retail's null
// branch; the ctor. Replaces the factory interpreter row (B39_RULES D2).
Object::Base* ListElement::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ListElement), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) ListElement();
}
// ?CreateInPlaceObject@ListElement@UI@@SAPEAVBase@Object@@PEAX@Z 0x180045100
Object::Base* ListElement::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) ListElement();
}

}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 UI-B: the slot doors, the ctor / dtor doors and the
// factory-interpreter row are gone. Their only readers were the minted
// `??_7ListElement@UI@@6B@`, its RT_DELDTOR row and FACTUI2: every live
// ListElement now wears the compiler's table.
// ============================================================================

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::ListElement's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@ListElement@UI@@ (0x1801fa120): name "ListElement", parent
// "Element" (retail ?_ParentTypeName@ListElement@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::ListElement, "Element", nullptr)
