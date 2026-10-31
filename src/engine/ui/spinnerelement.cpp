// ============================================================================
// src/engine/ui/spinnerelement.cpp — UI::SpinnerElement, the whole class
// ============================================================================
//
// `??_7SpinnerElement@UI@@6B@` (retail 0x180157660):
//
//   0  vector deleting dtor  0x18004b3b0  -> deleting_dtor.cpp row (this batch)
//   1  TypeName              0x18004b110  here
//   2  Type                  0x18004b0f0  here
//   4  ComputeSize           0x1800d83b0  here
//   5  ComputeLayout         0x1800d80c0  here
//   6  Draw                  0x1800d8030  here
//   7  Update                0x1800d7e70  here
//
// plus ??0SpinnerElement (0x1800d8670) and ??1SpinnerElement (0x1800d8640).
//
// ENDGAME B.3.6 block C5: the six slot bodies MOVED into the members of the
// real `UI::SpinnerElement`; each registered door is a one-line QUALIFIED
// delegation. The ctor / dtor stay doors (B4b-2).
//
// A spinner is THREE children and one int, exactly as
// src/engine/ui/SpinnerElement.h describes: children[0] is the
// NumberEditElement, children[1] the "up" ButtonElement and children[2] the
// "down" ButtonElement, each confirmed at run time by
// `?IsDerivedFrom@Base@Object@@` against its class's type id.
//
//   +0xC0  _increment  int32, ctor 1, ?SetIncrement@ 0x1800d7d30
//
// sizeof == 0xC8 (deleting dtor `MOV EDX,0xc8`).
//
// THE TWO TYPE IDS ARE READ, NOT ASSUMED: 0x1800d7eab loads
// `?_typeInfo@NumberEditElement@UI@@0VBaseTypeInfo@Object@@A` (0x1801fa190)
// and 0x1800d7ed4 / 0x1800d7f23 load
// `?_typeInfo@ButtonElement@UI@@0VBaseTypeInfo@Object@@A` (0x1801fa938); both
// VAs were resolved in the DLL's export directory, by address.
//
// A FAITHFUL CRASH THAT STAYS: when IsDerivedFrom says no, retail keeps the
// null in RDI (`XOR EDI,EDI; CMOVNZ RDI,RBX`) and then dereferences it for the
// vtable. That is retail's unguarded deref and CLAUDE.md says not to guard it.
// ============================================================================

#include <new>

#include "widget_common.h"
#include "ui/SpinnerElement.h"
#include "ui/ButtonElement.h"       // B.3.9 TI-UI: X::TypeId()
#include "ui/NumberEditElement.h"
#include "ui/UserInterface.h"

using namespace rtui;

namespace {

constexpr int kIncrement = 0xC0;
static_assert(kIncrement == offsetof(UI::SpinnerElement, _increment), "");

// EXT SEAM (NOT RETAIL): ext.phone_menu's side-by-side buttons (SpinnerElement.h `_extSideBySide`).
bool SideBySide() {
    const bool* on = UI::SpinnerElement::_extSideBySide;
    return on != nullptr && *on;
}

}  // namespace

namespace UI {

// EXT SEAM (NOT RETAIL, issue #73): ext.phone_menu's touch size, null by default (SpinnerElement.h);
// set by Ext::PhoneMenu::Sync.
const bool* SpinnerElement::_extSideBySide = nullptr;

// [4] 0x1800d83b0
void SpinnerElement::ComputeSize() {
    if ((_flags & kFlagNeedsSizeCalc) == 0) return;

    Element* const* kids = _children.GetData();

    int32_t w0 = 0, h0 = 0, w1 = 0, h1 = 0, w2 = 0, h2 = 0;
    InlineGetPaddedSize(kids[0], &w0, &h0);
    InlineGetPaddedSize(kids[1], &w1, &h1);
    InlineGetPaddedSize(kids[2], &w2, &h2);

    if (SideBySide()) {
        // EXT SEAM: the edit field, then up, then down, in one row.
        _layoutWidth = w0 + w1 + w2;
        int32_t h = h0;
        if (h < h1) h = h1;
        if (h < h2) h = h2;
        _flags &= ~kFlagNeedsSizeCalc;
        _layoutHeight = h;
        return;
    }

    if (w2 < w1) w2 = w1;
    _layoutWidth = w2 + w0;

    int32_t h = h2 + h1;
    if (h < h0) h = h0;
    _flags &= ~kFlagNeedsSizeCalc;
    _layoutHeight = h;
}

// [5] 0x1800d80c0
void SpinnerElement::ComputeLayout(int32_t x, int32_t y, int32_t right, int32_t bottom) {
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

    if ((_flags & kFlagStretchX) != 0) w = aw;
    if ((_flags & kFlagStretchY) != 0) h = ah;

    switch (align) {
        case 1: case 4: case 7: px = ((aw >> 1) - (w >> 1)) + px; break;
        case 2: case 5: case 8: px = (right - _rightPad) - w; break;
        default: break;
    }
    if (align > 2) {
        if (align < 6)      py = ((ah >> 1) - (h >> 1)) + py;
        else if (align < 9) py = (bottom - _bottomPad) - h;
    }

    _layoutTop    = py;
    _layoutLeft   = px;
    _layoutRight  = px + w;
    _layoutBottom = py + h;

    Element* const* kids = _children.GetData();
    int32_t ew = 0, eh = 0;
    InlineGetPaddedSize(kids[0], &ew, &eh);          // only the WIDTH is used
    int32_t bw = 0, bh = 0;
    InlineGetPaddedSize(kids[1], &bw, &bh);          // only the HEIGHT is used (retail's arm)
    int32_t dw = 0, dh = 0;
    InlineGetPaddedSize(kids[2], &dw, &dh);          // measured and discarded

    const int32_t xmid = ew + _layoutLeft;
    const int32_t ymid = _layoutTop + bh;

    if (SideBySide()) {
        // EXT SEAM: up at the edit field's right, down at up's right.
        const int32_t xdown = xmid + bw;
        _children.GetData()[0]->ComputeLayout(_layoutLeft, _layoutTop, xmid, _layoutBottom);
        _children.GetData()[1]->ComputeLayout(xmid, _layoutTop, xdown, _layoutBottom);
        _children.GetData()[2]->ComputeLayout(xdown, _layoutTop, _layoutRight, _layoutBottom);
        return;
    }

    // The array is RE-READ before each of the three calls (slot 5, virtual).
    _children.GetData()[0]->ComputeLayout(_layoutLeft, _layoutTop, xmid, _layoutBottom);
    _children.GetData()[1]->ComputeLayout(xmid, _layoutTop, _layoutRight, ymid);
    _children.GetData()[2]->ComputeLayout(xmid, ymid, _layoutRight, _layoutBottom);
}

// [6] 0x1800d8030
void SpinnerElement::Draw(SpriteContext& context, uint8_t alpha) const {
    if (_border != nullptr)
        _border->Draw(context, alpha);

    const int32_t n = _children.GetSize();
    for (int32_t i = 0; i < n; ++i)
        _children.GetData()[i]->Draw(context, alpha);   // slot 6
}

// ---------------------------------------------------------------------------
// [7] ?Update@SpinnerElement@UI@@ ...   0x1800d7e70
// ---------------------------------------------------------------------------
void SpinnerElement::Update(const System::Time& time, UserInterface* ui,
                            Object::InternalPtr<DialogInstance>* dialog,
                            const UpdateState& state) {
    // The base Update, QUALIFIED (this object's slot 7 is this very body), with
    // the handle copied into a temporary whose ADDRESS is passed — retail's shape.
    Object::InternalPtr<DialogInstance> slot = *dialog;
    Element::Update(time, ui, &slot, state);

    const uint32_t kNumberEditTypeId = UI::NumberEditElement::TypeId();   // retail [0x1801fa190]
    const uint32_t kButtonTypeId     = UI::ButtonElement::TypeId();       // retail [0x1801fa938]

    Element* const editRaw = _children.GetData()[0];
    if (!editRaw->IsDerivedFrom(kNumberEditTypeId)) return;
    // Checked just above: children[0] is a NumberEditElement (B10).
    NumberEditElement* const edit = static_cast<NumberEditElement*>(editRaw);
    if (edit == nullptr) return;

    // The "up" button is enabled only while the value is below the maximum.
    // `up` may be null (a failed IsDerivedFrom) and is dereferenced anyway —
    // retail's unguarded deref (`XOR EDI,EDI; CMOVNZ RDI,RBX`), kept.
    Element* upRaw = _children.GetData()[1];
    Element* up = upRaw->IsDerivedFrom(kButtonTypeId)
                  ? upRaw : nullptr;
    {
        const int32_t v = System::String::ToInteger(edit->_text._data);
        up->Enable(v < edit->_max);                          // slot 8
    }

    // ... and the "down" button only while it is above the minimum.
    Element* downRaw = _children.GetData()[2];
    Element* down = downRaw->IsDerivedFrom(kButtonTypeId)
                    ? downRaw : nullptr;
    {
        const int32_t v = System::String::ToInteger(edit->_text._data);
        down->Enable(edit->_min < v);                        // slot 8
    }

    int32_t step;
    if ((_children.GetData()[1]->_flags & kFlagWasActivated) != 0) {
        step = _increment;
    } else if ((_children.GetData()[2]->_flags & kFlagWasActivated) != 0) {
        step = -_increment;
    } else {
        return;
    }
    if (step == 0) return;

    const int32_t v = System::String::ToInteger(edit->_text._data);
    // ?SetValue@NumberEditElement@UI@@ (0x1800d4bb0).
    edit->SetValue(step + v);

    UserInterface::Event ev;
    ev.dialog   = dialog->Get();
    ev.element  = this;
    ev.eventId  = 0x15;
    ev.arg14    = _elementId;
    ev.arg18    = state.mouseX;
    ev.arg1C    = state.mouseY;
    ev.arg20    = 0;
    ev.userData = nullptr;
    rtcoll::RtColl_ArraySetSizeFill_180033690(&ui->_events, ui->_events._size + 1, &ev);
}

// ??1SpinnerElement@UI@@UEAA@XZ   0x1800d8640 — retail's body is the restamp
// and the tail into ~Element: the compiler's chain is all of it.
SpinnerElement::~SpinnerElement() {}

// ?CreateNewObject@SpinnerElement@UI@@SAPEAVBase@Object@@XZ 0x18004b200 --
// `?MemAlloc@MemoryAllocator` (0x1800c7a50) of 0xc8, align 8; retail's null
// branch; the ctor. Replaces the factory interpreter row (B39_RULES D2).
Object::Base* SpinnerElement::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(SpinnerElement), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) SpinnerElement();
}
// ?CreateInPlaceObject@SpinnerElement@UI@@SAPEAVBase@Object@@PEAX@Z 0x18004b290
Object::Base* SpinnerElement::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) SpinnerElement();
}

}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 UI-B: the slot doors, the ctor / dtor doors and the
// factory-interpreter row are gone. Their only readers were the minted
// `??_7SpinnerElement@UI@@6B@`, its RT_DELDTOR row and FACTUI2: every live
// SpinnerElement now wears the compiler's table.
// ============================================================================

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::SpinnerElement's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@SpinnerElement@UI@@ (0x1801fa3c0): name "SpinnerElement", parent
// "Element" (retail ?_ParentTypeName@SpinnerElement@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::SpinnerElement, "Element", nullptr)
