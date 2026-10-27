// ============================================================================
// src/engine/ui/sliderelement.cpp — UI::SliderElement, the whole class
// ============================================================================
//
// `??_7SliderElement@UI@@6B@` (retail 0x180157580):
//
//   0  vector deleting dtor  0x18004a180  -> deleting_dtor.cpp row (this batch)
//   1  TypeName              0x180049d20  here
//   2  Type                  0x180049d00  here
//   4  ComputeSize           0x1800d7380  here
//   5  ComputeLayout         0x1800d6fc0  here
//   6  Draw                  0x1800d6d40  here
//   7  Update                0x1800d67e0  here
//
// plus ??0SliderElement (0x1800d7570) and ??1SliderElement (0x180049ee0).
//
// NOT HERE: `?UpdateNumber@SliderElement@UI@@AEAAXXZ` (0x1800d6c20) — on the
// UI non-slot callee list; Update tail-calls it by address. Nor
// ?GetSliderBounds@ (0x1800d7270) or ?SetParameter@ (0x1800d6ca0).
//
// LAYOUT: src/engine/ui/SliderElement.h (sizeof 0x130, every field a
// store in the ctor at 0x1800d7570).
//
// ENDGAME B.3.6 block C5: the six slot bodies MOVED into the members of the
// real `UI::SliderElement`; each registered door is a one-line QUALIFIED
// delegation. The ctor / dtor stay doors (B4b-2).
//
// ~SliderElement destroys nothing and does NOT restamp the vtable — five
// instructions at 0x180049ee0, a bare tail jump to ~Element.
// ============================================================================

#include <new>

#include "widget_common.h"
#include "ui/SliderElement.h"
#include "ui/SpriteContext.h"
#include "ui/UserInterface.h"

using namespace rtui;

namespace {

constexpr int kTrackStart  = 0xC0;
constexpr int kTrackMiddle = 0xD0;
constexpr int kTrackEnd    = 0xE0;
constexpr int kThumb       = 0xF0;
constexpr int kThumbHot    = 0x100;
constexpr int kTrackOffY   = 0x110;
constexpr int kThumbMargin = 0x114;
constexpr int kMinimum     = 0x118;
constexpr int kMaximum     = 0x11C;
constexpr int kSteps       = 0x120;
constexpr int kHovered     = 0x124;
constexpr int kValue       = 0x128;
static_assert(kTrackStart  == offsetof(UI::SliderElement, _trackStart), "");
static_assert(kThumbHot    == offsetof(UI::SliderElement, _thumbHot), "");
static_assert(kTrackOffY   == offsetof(UI::SliderElement, _trackOffsetY), "");
static_assert(kThumbMargin == offsetof(UI::SliderElement, _thumbMargin), "");
static_assert(kMinimum     == offsetof(UI::SliderElement, _minimum), "");
static_assert(kMaximum     == offsetof(UI::SliderElement, _maximum), "");
static_assert(kSteps       == offsetof(UI::SliderElement, _steps), "");
static_assert(kHovered     == offsetof(UI::SliderElement, _hovered), "");
static_assert(kValue       == offsetof(UI::SliderElement, _value), "");


// ?UpdateNumber@SliderElement@UI@@AEAAXXZ (0x1800d6c20) — its body is
// element_bodies_b5.cpp's door (fragment pass); reached by address.


// The thumb travel, computed identically by ComputeLayout, Draw and Update:
//   span = (_layoutRight - 2*(thumbW/2 + _thumbMargin)) - _layoutLeft
//   pos  = span * ((_value - _minimum) / (_maximum - _minimum))      (0 if flat)
int32_t ThumbOffset(const UI::SliderElement& s, int32_t thumbW, int32_t margin) {
    const float range = s._maximum - s._minimum;
    if (range == 0.0f) return 0;
    const int32_t span = (s._layoutRight + ((thumbW >> 1) + margin) * -2)
                         - s._layoutLeft;
    return static_cast<int32_t>(static_cast<float>(span)
                                * ((s._value - s._minimum) / range));
}

// EXT SEAM (NOT RETAIL): ext.phone_menu's touch size (SliderElement.h `_extSpriteScale`). The factor
// every sprite extent, the track offset and the thumb margin are multiplied by; 1 (null) is retail's.
int32_t SpriteScale() {
    const int32_t* k = UI::SliderElement::_extSpriteScale;
    if (k == nullptr || *k < 1) return 1;
    return *k;
}

void QueueEvent(UI::SliderElement* self, UI::UserInterface* ui,
                Object::InternalPtr<UI::DialogInstance>* dialog,
                const UI::Element::UpdateState& state) {
    UI::UserInterface::Event ev;
    ev.dialog   = dialog->Get();
    ev.element  = self;
    ev.eventId  = 0x17;
    ev.arg14    = self->_elementId;
    ev.arg18    = state.mouseX;
    ev.arg1C    = state.mouseY;
    ev.arg20    = 0;
    ev.userData = nullptr;
    rtcoll::RtColl_ArraySetSizeFill_180033690(&ui->_events, ui->_events._size + 1, &ev);
}

}  // namespace

namespace UI {

// [4] 0x1800d7380
void SliderElement::ComputeSize() {
    if ((_flags & kFlagNeedsSizeCalc) == 0) return;

    const int32_t k = SpriteScale();   // EXT SEAM: 1 = retail
    const int32_t trackW = (_trackStart.Width() + _trackMiddle.Width() + _trackEnd.Width()) * k;
    int32_t width = _thumb.Width() * k;

    int32_t height = 0;
    if (_children.GetSize() > 0) {
        Element* child = _children.GetData()[0];
        if (child != nullptr) {
            int32_t cw = 0;
            InlineGetPaddedSize(child, &cw, &height);
        }
    }

    if (width < trackW) width = trackW;
    _layoutWidth = width;

    int32_t spriteH = _trackStart.Height() * k;
    const int32_t thumbH = _thumb.Height() * k;
    if (spriteH < thumbH) spriteH = thumbH;
    if (height  < spriteH) height = spriteH;

    _flags &= ~kFlagNeedsSizeCalc;
    _layoutHeight = height;
}

// [5] 0x1800d6fc0
void SliderElement::ComputeLayout(int32_t x, int32_t y, int32_t right, int32_t bottom) {
    if (_border != nullptr)
        _border->ComputeLayout(x, y, right, bottom);

    ComputeSize();   // slot 4, VIRTUAL — `CALL [RAX+0x20]` in retail

    int32_t h = _layoutHeight;
    const int32_t align = _alignment;
    if (right  < _layoutWidth + x) x = right - _layoutWidth;
    if (bottom < h + y) y = bottom - h;

    int32_t px = x + _leftPad;
    int32_t py = y + _topPad;
    const int32_t ah = (bottom - _bottomPad) - py;
    const int32_t aw = (right  - _rightPad)  - px;

    if ((_flags & kFlagStretchY) != 0) h = ah;

    switch (align) {
        case 2: case 5: case 8:
            px = (right - _rightPad) - aw;
            break;
        default: break;
    }
    if (align > 2) {
        if (align < 6)      py = ((ah >> 1) - (h >> 1)) + py;
        else if (align < 9) py = (bottom - _bottomPad) - h;
    }

    _layoutTop    = py;
    _layoutLeft   = px;
    _layoutRight  = aw + px;
    _layoutBottom = py + h;

    Element* child = _children.GetData()[0];
    if (child == nullptr) return;

    int32_t cw = 0, ch = 0;
    InlineGetPaddedSize(child, &cw, &ch);

    const int32_t k = SpriteScale();   // EXT SEAM: 1 = retail
    const int32_t thumbW = _thumb.Width() * k;
    const int32_t margin = _thumbMargin * k;
    int32_t pos = _layoutLeft + margin + ThumbOffset(*this, thumbW, margin);

    int32_t q = thumbW + pos;
    if (q + cw < _layoutRight) q = margin + q;
    else                       q = pos - cw;

    _children.GetData()[0]->ComputeLayout(q, _trackOffsetY * k + _layoutTop,
                                          q + cw, _layoutBottom);   // slot 5
}

// [6] 0x1800d6d40
void SliderElement::Draw(SpriteContext& context, uint8_t alpha) const {
    if (_border != nullptr)
        _border->Draw(context, alpha);

    const uint32_t raw = _color;
    if ((_flags & kFlagDimColor) != 0) alpha >>= 1;
    const uint32_t c = ModulateColor(raw, alpha);

    const int32_t left   = _layoutLeft;
    const int32_t rightX = _layoutRight;
    const int32_t top    = _layoutTop;
    const int32_t k      = SpriteScale();   // EXT SEAM: 1 = retail
    const int32_t ty     = top + _trackOffsetY * k;

    if (k != 1) {
        // EXT SEAM (NOT RETAIL): ext.phone_menu's touch size, every sprite k times its size (rect
        // draws); retail's point draws below.
        const int32_t sttW = _trackStart.Width() * k;
        const int32_t endW = _trackEnd.Width() * k;
        context.AddSprite(_trackStart, left, ty, left + sttW, ty + _trackStart.Height() * k, c);
        context.AddSprite(_trackMiddle, left + sttW, ty, rightX - endW, ty + _trackMiddle.Height() * k, c);
        context.AddSprite(_trackEnd, rightX - endW, ty, rightX, ty + _trackEnd.Height() * k, c);
        const int32_t margin = _thumbMargin * k;
        const int32_t pos = ThumbOffset(*this, _thumb.Width() * k, margin);
        const ::Sprite& thumb = (_hovered != 0) ? _thumbHot : _thumb;
        const int32_t tx = margin + pos + left;
        context.AddSprite(thumb, tx, top, tx + thumb.Width() * k, top + thumb.Height() * k, c);
        const Element* child = _children.GetData()[0];
        if (child != nullptr)
            child->Draw(context, alpha);   // slot 6
        return;
    }

    // UI::SpriteContext::AddSprite (block D's class) — by address, doors stay.
    context.AddSprite(_trackStart, left, ty, c);

    const int32_t midH = _trackMiddle.Height();
    const int32_t endW = _trackEnd.Width();
    const int32_t sttW = _trackStart.Width();
    context.AddSprite(_trackMiddle, left + sttW, ty, rightX - endW, ty + midH, c);

    context.AddSprite(_trackEnd, rightX - _trackEnd.Width(), ty, c);

    // The thumb's TRAVEL is measured with the idle sprite's width even when
    // the hot sprite is the one drawn (0x1800d6f3d reads +0xF0 before the
    // hover swap at 0x1800d6f5f). Reproduced.
    const int32_t pos = ThumbOffset(*this, _thumb.Width(), _thumbMargin);
    const ::Sprite& thumb = (_hovered != 0) ? _thumbHot : _thumb;
    context.AddSprite(thumb, _thumbMargin + pos + left, top, c);

    const Element* child = _children.GetData()[0];
    if (child != nullptr)
        child->Draw(context, alpha);   // slot 6
}

// ---------------------------------------------------------------------------
// [7] ?Update@SliderElement@UI@@ ...   0x1800d67e0
// ---------------------------------------------------------------------------
void SliderElement::Update(const System::Time& time, UserInterface* ui,
                           Object::InternalPtr<DialogInstance>* dialog,
                           const UpdateState& state) {
    // The base Update, QUALIFIED (this object's slot 7 is this very body), with
    // the handle copied into a temporary whose ADDRESS is passed — retail's shape.
    Object::InternalPtr<DialogInstance> slot = *dialog;
    Element::Update(time, ui, &slot, state);

    const int32_t k      = SpriteScale();   // EXT SEAM: 1 = retail
    const int32_t thumbW = _thumb.Width() * k;
    const int32_t thumbH = _thumb.Height() * k;
    const int32_t margin = _thumbMargin * k;
    const float   range  = _maximum - _minimum;

    const int32_t pos = _layoutLeft + margin + ThumbOffset(*this, thumbW, margin);

    const int32_t mx = state.mouseX;
    const int32_t my = state.mouseY;
    const bool live  = state.buttonPressed != 0 && (_flags & kFlagInteractive) != 0;

    if (mx < pos || pos + thumbW < mx ||
        my < _layoutTop || _layoutTop + thumbH < my) {
        _hovered = 0;
        if (live) {
            bool moved = true;
            if (pos + thumbW < mx) {
                _value = range / static_cast<float>(_steps) + _value;
            } else if (mx < pos) {
                _value = _value - range / static_cast<float>(_steps);
            } else {
                moved = false;   // inside the thumb band horizontally: no page
            }
            if (moved) {
                QueueEvent(this, ui, dialog, state);
                static_cast<UI::UserInterface*>(ui)->Play(4);
            }
        }
    } else {
        _hovered = 1;
        if (live) {
            _dragAnchor = _value;
            SetFlags(this, _flags | kFlagDragging);
            static_cast<UI::UserInterface*>(ui)->Play(4);
        }
    }

    bool isNear = !(mx < _layoutLeft   - 0x20 ||
                    _layoutRight + 0x20 < mx  ||
                    my < _layoutTop    - 0x20 ||
                    _layoutBottom + 0x20 < my);

    const uint32_t flags = _flags;
    if ((flags & kFlagDragging) != 0 && !isNear) {
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/SLIDERCLAMP.md): fix.slider_drag_clamp -- a drag
        // off the band is treated as on it: the thumb keeps following the pointer (clamped below) and a
        // release there keeps the value. Off / null = retail's snap back to the anchor.
        if (_extFixDragClamp != nullptr && *_extFixDragClamp) {
            isNear = true;
        } else {
            _value = _dragAnchor;
            UpdateNumber();                                                 // 0x1800d6c20
            return;
        }
    }

    bool doDrag = true;
    if ((flags & kFlagDragging) != 0 && state.b04 != 0) {       // +0x04: the release
        SetFlags(this, flags & ~kFlagDragging);
        if (isNear) static_cast<UI::UserInterface*>(ui)->Play(5);
        else        doDrag = false;
    }

    if (doDrag && isNear && (_flags & kFlagDragging) != 0) {
        const int32_t w = _thumb.Width() * k;
        const int32_t span = (_layoutRight + ((w >> 1) + margin) * -2) - _layoutLeft;
        const float v = _dragAnchor
            - (static_cast<float>(state.dragDx) / static_cast<float>(span))
              * (_maximum - _minimum);
        if (v != _value) {
            _value = v;
            QueueEvent(this, ui, dialog, state);
        }
    }

    if (_value < _minimum) _value = _minimum;
    if (_maximum < _value) _value = _maximum;

    UpdateNumber();                                                         // 0x1800d6c20
}

// EXT SEAM (NOT RETAIL, docs/extensions/handins/SLIDERCLAMP.md): fix.slider_drag_clamp, null by default
// (SliderElement.h); set by Ext::InitFromSettings.
const bool* SliderElement::_extFixDragClamp = nullptr;

// EXT SEAM (NOT RETAIL, docs/android/P11_TOUCH_SIZE.md): ext.phone_menu's touch size, null by default
// (SliderElement.h); set by Ext::PhoneMenu::Sync.
const int32_t* SliderElement::_extSpriteScale = nullptr;

// ??1SliderElement@UI@@UEAA@XZ   0x180049ee0 — retail's body is a BARE TAIL
// JUMP to ~Element with no restamp: the compiler's chain is all of it.
SliderElement::~SliderElement() {}

// ?CreateNewObject@SliderElement@UI@@SAPEAVBase@Object@@XZ 0x180049e10 --
// `?MemAlloc@MemoryAllocator` (0x1800c7a50) of 0x130, align 8; retail's null
// branch; the ctor. Replaces the factory interpreter row (B39_RULES D2).
Object::Base* SliderElement::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(SliderElement), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) SliderElement();
}
// ?CreateInPlaceObject@SliderElement@UI@@SAPEAVBase@Object@@PEAX@Z 0x180049e90
Object::Base* SliderElement::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) SliderElement();
}

}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 UI-B: the slot doors, the ctor / dtor doors and the
// factory-interpreter row are gone. Their only readers were the minted
// `??_7SliderElement@UI@@6B@`, its RT_DELDTOR row and FACTUI2: every live
// SliderElement now wears the compiler's table.
// ============================================================================

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::SliderElement's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@SliderElement@UI@@ (0x1801fa350): name "SliderElement", parent
// "Element" (retail ?_ParentTypeName@SliderElement@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::SliderElement, "Element", nullptr)
