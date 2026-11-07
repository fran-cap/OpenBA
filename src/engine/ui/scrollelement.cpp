// ============================================================================
// src/engine/ui/scrollelement.cpp — UI::ScrollElement, the whole class
// ============================================================================
//
// `??_7ScrollElement@UI@@6B@` (retail 0x180156400):
//
//   0  vector deleting dtor  0x18003adc0  -> deleting_dtor.cpp row (this batch)
//   1  TypeName              0x18003a850  here
//   2  Type                  0x18003a830  here
//   4  ComputeSize           0x18011ad80  here
//   5  ComputeLayout         0x18011af20  here
//   6  Draw                  0x18011b0b0  here
//   7  Update                0x18011b620  here
//
// plus ??0ScrollElement (0x18011a7f0) and ??1ScrollElement (0x18011a900).
//
// NOT HERE: `?GetBarLayout@ScrollElement@UI@@AEBAXAEAH000@Z` (0x18011a2d0) —
// on the UI non-slot callee list, reached by address by both Draw and Update.
// Nor the three one-line setters 0x18003a9d0/0x18003a9f0/0x18003aa10.
//
// ---------------------------------------------------------------------------
// LAYOUT. sizeof == 0x178 (deleting dtor `MOV EDX,0x178`). NINE UI::Sprites in
// three 3-slice sets, then six scalars — every one written by the ctor at
// 0x18011a7f0:
//
//   +0xC0 / +0xD0 / +0xE0    _track{Start,Middle,End}   the groove
//   +0xF0 / +0x100 / +0x110  _thumb{Start,Middle,End}   the thumb, idle
//   +0x120 / +0x130 / +0x140 _hot{Start,Middle,End}     the thumb, live
//   +0x150  _vertical        bool,   ctor 1
//   +0x154  _barColor        uint32, ctor 0xFFFFFFFF   } one qword store
//   +0x158  (unnamed)        uint32, ctor 0xFFFFFFFF   } of 0xFFFFFFFFFFFFFFFF
//   +0x160  _current         float,  ctor 0
//   +0x164  _displayPercent  float,  ctor 0.33333334f  ?SetDisplayPercent@
//   +0x168  _maxValue        float,  ctor 4.0f         ?SetMaxValue@
//   +0x16C  _dragAnchor      float,  ctor 0 (the qword store at +0x168)
//   +0x170  _dragValue       float,  ctor 0
//
// ~ScrollElement destroys NOTHING — it restamps the vtable and tail-calls
// ~Element. Nine Sprites are nine {ExternalPtr, index} pairs and own no heap.
//
// The float 1.0f that Update divides by is the rodata constant at
// 0x18016169c, read out of the shipped image (`00 00 80 3f`) rather than
// assumed from the shape of the expression.
//
// ENDGAME B.3.6 block C4: the layout above moved to src/engine/ui/
// ScrollElement.h (the real class). Every slot body below is a MEMBER; each
// registered door is a one-line QUALIFIED delegation; ctor/dtor stay DOORS.
// RULE B6-5: the vtable and type-info record are cached in namespace-scope
// std::atomic slots.
// ============================================================================
#include <new>

#include "widget_common.h"
#include "ui/ScrollElement.h"
#include "ui/SpriteContext.h"
#include "ui/UserInterface.h"

using namespace rtui;

namespace {

// UpdateState fields these bodies read (UI::Element::UpdateState names):
//   buttonPressed +0x01, b04 (released) +0x04, dragDx +0x0c, dragDy +0x10,
//   mouseX +0x18, mouseY +0x1c.

// 0x18016169c — `00 00 80 3f`, read from the shipped .rdata.
constexpr float kOne = 1.0f;

// `?GetBarLayout@ScrollElement@UI@@AEBAXAEAH000@Z` (0x18011a2d0) — declared on
// the class, body still element_bodies_b5.cpp's door (fragment pass).

}  // namespace

// ============================================================================
// The member bodies (ENDGAME B.3.6 block C4)
// ============================================================================
namespace UI {

// ---------------------------------------------------------------------------
// slot 4 — ?ComputeSize@ScrollElement@UI@@UEAAXXZ   0x18011ad80
// ---------------------------------------------------------------------------
void ScrollElement::ComputeSize() {
    const uint32_t flags = _flags;
    if ((flags & UI::kElementFlagNeedsSizeCalc) == 0) return;

    if (_vertical != 0) {
        _layoutWidth = _trackStart.Width();
        const int32_t h = _trackStart.Height()
                        + _trackMiddle.Height()
                        + _trackEnd.Height();
        _flags = flags & ~UI::kElementFlagNeedsSizeCalc;
        _layoutHeight = h;
        return;
    }

    _layoutWidth = _trackStart.Width()
                 + _trackMiddle.Width()
                 + _trackEnd.Width();
    _layoutHeight = _trackStart.Height();
    _flags = flags & ~UI::kElementFlagNeedsSizeCalc;
}

// ---------------------------------------------------------------------------
// slot 5 — ?ComputeLayout@ScrollElement@UI@@UEAAXHHHH@Z   0x18011af20
//
// `_vertical` is read BEFORE ComputeSize runs (0x18011af3c), which matters: a
// ComputeSize that flipped it would not be seen here. Reproduced.
// ---------------------------------------------------------------------------
void ScrollElement::ComputeLayout(int32_t x, int32_t y, int32_t right, int32_t bottom) {
    if (Element* border = _border)
        border->ComputeLayout(x, y, right, bottom);

    const uint8_t vertical = _vertical;
    ComputeSize();                                            // slot 4, virtual

    int32_t w = _layoutWidth;
    int32_t h = _layoutHeight;

    const bool stretchY = !(vertical == 0 && (_flags & UI::kElementFlagAlignArg2) == 0);
    const bool stretchX =  (vertical == 0 || (_flags & UI::kElementFlagAlignArg1) != 0);

    const int32_t align = _alignment;
    if (right  < w + x) x = right  - w;
    if (bottom < h + y) y = bottom - h;

    int32_t px = x + _leftPad;
    int32_t py = y + _topPad;
    const int32_t aw = (right  - _rightPad)  - px;
    const int32_t ah = (bottom - _bottomPad) - py;

    if (stretchX) w = aw;
    if (stretchY) h = ah;

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
    _layoutTop    = py;
    _layoutLeft   = px;
    _layoutBottom = py + h;
}

// ---------------------------------------------------------------------------
// slot 6 — ?Draw@ScrollElement@UI@@UEBAXAEAVSpriteContext@2@E@Z   0x18011b0b0
//
// The alpha is halved TWICE when the dim flag is set: once before the track
// colour and again before the bar colour, on the same register. Retail's.
// ---------------------------------------------------------------------------
void ScrollElement::Draw(SpriteContext& context, uint8_t alpha) const {
    if (Element* border = _border)
        border->Draw(context, alpha);

    uint32_t raw = _color;
    if ((_flags & UI::kElementFlagDimColor) != 0) alpha >>= 1;
    const uint32_t trackColor = ModulateColor(raw, alpha);

    const int32_t left   = _layoutLeft;
    const int32_t top    = _layoutTop;
    const int32_t rightX = _layoutRight;
    const int32_t botY   = _layoutBottom;

    int32_t ex0, ey0, ex1, ey1;    // the END slice's rectangle, common tail
    if (_vertical == 0) {
        const int32_t w0 = _trackStart.Width();
        context.AddSprite(_trackStart, left, top, left + w0, botY, trackColor);
        const int32_t w2 = _trackEnd.Width();
        const int32_t w0b = _trackStart.Width();
        context.AddSprite(_trackMiddle, left + w0b, top, rightX - w2, botY, trackColor);
        ex0 = rightX - _trackEnd.Width();
        ey0 = top;
        ex1 = rightX;
        ey1 = botY;
    } else {
        const int32_t h0 = _trackStart.Height();
        context.AddSprite(_trackStart, left, top, rightX, top + h0, trackColor);
        const int32_t h2 = _trackEnd.Height();
        const int32_t h0b = _trackStart.Height();
        context.AddSprite(_trackMiddle, left, top + h0b, rightX, botY - h2, trackColor);
        ex0 = left;
        ey0 = botY - _trackEnd.Height();
        ex1 = rightX;
        ey1 = botY;
    }
    context.AddSprite(_trackEnd, ex0, ey0, ex1, ey1, trackColor);

    raw = _barColor;
    if ((_flags & UI::kElementFlagDimColor) != 0) alpha >>= 1;
    const uint32_t barColor = ModulateColor(raw, alpha);

    int32_t bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
    GetBarLayout(bx0, by0, bx1, by1);                 // 0x18011a2d0

    // The thumb uses the HOT set while the element is interactive and the
    // scroll actually has range.
    const bool hot = (_flags & UI::kElementFlagHovered) != 0 && _maxValue > 0.0f;
    // Each thumb slice is a 0x10-byte UI::Sprite copied to the stack (two
    // qword loads), exactly as Draw does.
    const UI::Sprite s0 = hot ? _hotStart  : _thumbStart;
    const UI::Sprite s1 = hot ? _hotMiddle : _thumbMiddle;
    const UI::Sprite s2 = hot ? _hotEnd    : _thumbEnd;

    if (_vertical == 0) {
        const int32_t w0 = s0.Width();
        context.AddSprite(s0, bx0, by0, bx0 + w0, by1, barColor);
        const int32_t w2  = s2.Width();
        const int32_t w0b = s0.Width();
        context.AddSprite(s1, bx0 + w0b, by0, bx1 - w2, by1, barColor);
        bx0 = bx1 - s2.Width();
    } else {
        const int32_t h0 = s0.Height();
        context.AddSprite(s0, bx0, by0, bx1, by0 + h0, barColor);
        const int32_t h2  = s2.Height();
        const int32_t h0b = s0.Height();
        context.AddSprite(s1, bx0, by0 + h0b, bx1, by1 - h2, barColor);
        by0 = by1 - s2.Height();
    }
    context.AddSprite(s2, bx0, by0, bx1, by1, barColor);
}

// ---------------------------------------------------------------------------
// slot 7 — ?Update@ScrollElement@UI@@ ...   0x18011b620
// ---------------------------------------------------------------------------
void ScrollElement::Update(const System::Time& time, UserInterface* ui,
                           Object::InternalPtr<DialogInstance>* dialog,
                           const UpdateState& state) {
    Object::InternalPtr<DialogInstance> dialogCopy = *dialog;
    UI::Element::Update(time, ui, &dialogCopy, state);         // qualified: 0x180116270

    // Nothing to scroll and the default display fraction: park at zero.
    if (_maxValue == 0.0f && _displayPercent == kOne) {
        _currentValue = 0.0f;
        return;
    }

    if (state.buttonPressed != 0 && (_flags & UI::kElementFlagHovered) != 0) {
        int32_t bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
        GetBarLayout(bx0, by0, bx1, by1);                 // 0x18011a2d0

        const int32_t mx = state.mouseX;
        const int32_t my = state.mouseY;
        const bool vertical = _vertical != 0;
        bool played = false;

        if (mx < bx0 || bx1 < mx || my < by0 || by1 < my) {
            // Outside the thumb: page toward the click by one visible page.
            const float page = _maxValue / (kOne / _displayPercent - kOne);
            if ((vertical && by1 < my) || bx1 < mx) {
                _currentValue = _currentValue + page;
                played = true;
            } else if ((vertical && my < by0) || mx < bx0) {
                _currentValue = _currentValue - page;
                played = true;
            }
        } else {
            _dragAnchor = _currentValue;
            SetFlags(this, _flags | UI::kElementFlagPressed);
            played = true;
        }

        if (played) static_cast<UI::UserInterface*>(ui)->Play(4);

        if (_currentValue < 0.0f) _currentValue = 0.0f;
        if (_maxValue < _currentValue) _currentValue = _maxValue;
    }

    const int32_t mx = state.mouseX;
    const int32_t my = state.mouseY;
    bool isNear = !(mx < _layoutLeft   - 0x20 ||
                    _layoutRight + 0x20 < mx  ||
                    my < _layoutTop    - 0x20 ||
                    _layoutBottom + 0x20 < my);

    const uint32_t flags = _flags;
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/SLIDERCLAMP.md): fix.slider_drag_clamp -- a drag
    // off the band is treated as on it: the thumb keeps following the pointer (clamped in the drag
    // arm) and a release there keeps the value. Off / null = retail's snap back to the anchor below.
    if ((flags & UI::kElementFlagPressed) != 0 && !isNear && _extFixDragClamp != nullptr && *_extFixDragClamp) {
        isNear = true;
    }
    if ((flags & UI::kElementFlagPressed) == 0 || isNear) {
        if ((flags & UI::kElementFlagPressed) != 0 && state.b04 != 0) {
            SetFlags(this, flags & ~UI::kElementFlagPressed);
            if (!isNear) return;
            static_cast<UI::UserInterface*>(ui)->Play(5);
        }
        if (isNear && (_flags & UI::kElementFlagPressed) != 0) {
            _currentValue = _dragValue;
            const bool vertical = _vertical != 0;
            const int32_t span = vertical ? (_layoutBottom - _layoutTop)
                                          : (_layoutRight  - _layoutLeft);
            const float maxV = _maxValue;
            const int32_t delta = vertical ? state.dragDy : state.dragDx;
            const float v = _dragAnchor
                - (static_cast<float>(delta)
                   / (static_cast<float>(span)
                      - static_cast<float>(span) * _displayPercent)) * maxV;
            _currentValue = v;
            if (v < 0.0f) _currentValue = 0.0f;
            if (maxV < _currentValue) _currentValue = maxV;
            _dragValue = _currentValue;
        }
    } else {
        _currentValue = _dragAnchor;
    }
}

// EXT SEAM (NOT RETAIL, docs/extensions/handins/SLIDERCLAMP.md): fix.slider_drag_clamp, null by default
// (ScrollElement.h); set by Ext::InitFromSettings.
const bool* ScrollElement::_extFixDragClamp = nullptr;

// ??1ScrollElement@UI@@UEAA@XZ   0x18011a900 — retail's body is the restamp
// and the tail into ~Element: the compiler's chain is all of it.
ScrollElement::~ScrollElement() {}

// ?CreateNewObject@ScrollElement@UI@@SAPEAVBase@Object@@XZ 0x18003a940 --
// `?MemAlloc@MemoryAllocator` (0x1800c7a50) of 0x178, align 8; retail's null
// branch; the ctor. Replaces the factory interpreter row (B39_RULES D2).
Object::Base* ScrollElement::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ScrollElement), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) ScrollElement();
}
// ?CreateInPlaceObject@ScrollElement@UI@@SAPEAVBase@Object@@PEAX@Z 0x18003a9c0
Object::Base* ScrollElement::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) ScrollElement();
}

}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 UI-B: the slot doors, the ctor / dtor doors and the
// factory-interpreter row are gone. Their only readers were the minted
// `??_7ScrollElement@UI@@6B@`, its RT_DELDTOR row and FACTUI2: every live
// ScrollElement now wears the compiler's table.
// ============================================================================

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::ScrollElement's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@ScrollElement@UI@@ (0x1801fad98): name "ScrollElement", parent
// "Element" (retail ?_ParentTypeName@ScrollElement@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::ScrollElement, "Element", nullptr)
