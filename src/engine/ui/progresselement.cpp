// ============================================================================
// src/engine/ui/progresselement.cpp — UI::ProgressElement, the whole class
// ============================================================================
//
// `??_7ProgressElement@UI@@6B@` (retail 0x180159180):
//
//   0  vector deleting dtor  0x180078150  -> deleting_dtor.cpp row (this batch)
//   1  TypeName              0x180077d40  here
//   2  Type                  0x180077d20  here
//   4  ComputeSize           0x1801198f0  here
//   5  ComputeLayout         0x180119610  here
//   6  Draw                  0x1801192c0  here
//
// plus ??0ProgressElement (0x180119c40) and ??1ProgressElement (0x180077ef0).
//
// LAYOUT: src/engine/ui/ProgressElement.h (sizeof 0x130; six UI::Sprites,
// the background colour and the two floats, every one written by the ctor).
//
// ENDGAME B.3.6 block C5: the five slot bodies MOVED into the members of the
// real `UI::ProgressElement`; each registered door is a one-line QUALIFIED
// delegation. The ctor / dtor stay doors (B4b-2).
//
// ~ProgressElement does NOT restamp the vtable — it is a bare tail jump to
// ~Element (five instructions at 0x180077ef0, disassembled). That asymmetry
// with its own ctor is retail's and is reproduced.
// ============================================================================


#include <new>

#include "widget_common.h"
#include "ui/ProgressElement.h"
#include "ui/SpriteContext.h"

using namespace rtui;

namespace {

constexpr int kFillLeft    = 0xC0;
constexpr int kFillMiddle  = 0xD0;
constexpr int kFillRight   = 0xE0;
constexpr int kTrackLeft   = 0xF0;
constexpr int kTrackMiddle = 0x100;
constexpr int kTrackRight  = 0x110;
constexpr int kBackColor   = 0x120;
constexpr int kMaxValue    = 0x124;
constexpr int kCurValue    = 0x128;

static_assert(kFillLeft    == offsetof(UI::ProgressElement, _fillLeft), "");
static_assert(kTrackRight  == offsetof(UI::ProgressElement, _trackRight), "");
static_assert(kBackColor   == offsetof(UI::ProgressElement, _backgroundColor), "");
static_assert(kMaxValue    == offsetof(UI::ProgressElement, _maxValue), "");
static_assert(kCurValue    == offsetof(UI::ProgressElement, _currentValue), "");

}  // namespace

namespace UI {

// ---------------------------------------------------------------------------
// [4] ?ComputeSize@ProgressElement@UI@@UEAAXXZ   0x1801198f0
// ---------------------------------------------------------------------------
void ProgressElement::ComputeSize() {
    if ((_flags & UI::kElementFlagNeedsSizeCalc) == 0) return;

    const int32_t fillW  = _fillLeft.Width() + _fillMiddle.Width() + _fillRight.Width();
    int32_t trackW = _trackLeft.Width() + _trackMiddle.Width() + _trackRight.Width();

    int32_t height = 0;
    if (_children.GetSize() > 0) {
        Element* child = _children.GetData()[0];
        if (child != nullptr) {
            int32_t cw = 0;
            InlineGetPaddedSize(child, &cw, &height);
        }
    }

    if (trackW < fillW) trackW = fillW;
    _layoutWidth = trackW;

    int32_t spriteH = _trackLeft.Height();
    const int32_t fillH = _fillLeft.Height();
    if (spriteH < fillH) spriteH = fillH;
    if (height  < spriteH) height = spriteH;

    _flags &= ~UI::kElementFlagNeedsSizeCalc;
    _layoutHeight = height;
}

// ---------------------------------------------------------------------------
// [5] ?ComputeLayout@ProgressElement@UI@@UEAAXHHHH@Z   0x180119610
// ---------------------------------------------------------------------------
void ProgressElement::ComputeLayout(int32_t x, int32_t y, int32_t right, int32_t bottom) {
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

    // Only the vertical stretch bit is tested; the width always stretches.
    if ((_flags & UI::kElementFlagAlignArg2) != 0) h = ah;

    switch (align) {
        case ElementDescription::TopRight:
        case ElementDescription::MidRight:
        case ElementDescription::BottomRight:
            px = (right - _rightPad) - aw;
            break;
        default:
            break;
    }
    if (align > ElementDescription::TopRight) {
        if (align < ElementDescription::BottomLeft)
            py = ((ah >> 1) - (h >> 1)) + py;
        else if (align <= ElementDescription::BottomRight)
            py = (bottom - _bottomPad) - h;
    }

    _layoutLeft   = px;
    _layoutRight  = aw + px;
    _layoutTop    = py;
    _layoutBottom = py + h;

    if (_children.GetSize() <= 0) return;
    Element* child = _children.GetData()[0];
    if (child == nullptr) return;

    int32_t cw = 0, ch = 0;
    InlineGetPaddedSize(child, &cw, &ch);

    int32_t left  = _layoutLeft;
    int32_t barW  = static_cast<int32_t>(
        (_currentValue / _maxValue) * static_cast<float>(_layoutRight - left));
    if (barW < _layoutWidth) barW = _layoutWidth;

    uint32_t color;
    if (_currentValue == 0.0f) {
        if (_fillLeft.spriteIndex < 0) {
            color = System::kColorWhite;
        } else {
            left += _fillLeft.Width();
            color = System::kColorWhite;
        }
    } else {
        const int32_t capW = _fillRight.Width();
        if (barW + cw < (_layoutRight - left) - capW) {
            color = System::kColorWhite;
        } else {
            color = System::kColorBlack;
            left  = (left - _fillRight.Width()) - cw;
        }
        left += barW;
    }

    // The child is RE-READ from the array before each of the two calls.
    _children.GetData()[0]->SetColor(color);                                  // slot 10
    _children.GetData()[0]->ComputeLayout(left, _layoutTop, left + cw, _layoutBottom);  // slot 5
}

// ---------------------------------------------------------------------------
// [6] ?Draw@ProgressElement@UI@@UEBAXAEAVSpriteContext@2@E@Z  0x1801192c0
//
// THE ALPHA IS HALVED TWICE when the dim flag is set. Retail tests
// `flags & 0x40000` once before each of the two colour modulations and shifts
// the SAME register both times, so a dimmed progress bar's fill is at a
// quarter alpha, not a half. Transcribed, not corrected.
// ---------------------------------------------------------------------------
void ProgressElement::Draw(SpriteContext& context, uint8_t alpha) const {
    if (_border != nullptr)
        _border->Draw(context, alpha);

    uint32_t raw = _backgroundColor;
    if ((_flags & UI::kElementFlagDimColor) != 0) alpha >>= 1;
    const uint32_t trackColor = ModulateColor(raw, alpha);

    raw = _color;
    if ((_flags & UI::kElementFlagDimColor) != 0) alpha >>= 1;
    const uint32_t fillColor = ModulateColor(raw, alpha);

    const int32_t left   = _layoutLeft;
    const int32_t top    = _layoutTop;
    const int32_t rightX = _layoutRight;

    // UI::SpriteContext::AddSprite (block D's class) — by address, doors stay.
    context.AddSprite(_trackLeft, left, top, trackColor);

    const int32_t midH   = _trackMiddle.Height();
    const int32_t rightW = _trackRight.Width();
    const int32_t leftW  = _trackLeft.Width();
    context.AddSprite(_trackMiddle, left + leftW, top, rightX - rightW, top + midH, trackColor);

    context.AddSprite(_trackRight, rightX - _trackRight.Width(), top, trackColor);

    if (_currentValue > 0.0f) {
        int32_t barW = static_cast<int32_t>(
            (_currentValue / _maxValue) * static_cast<float>(rightX - left));
        if (barW < _layoutWidth) barW = _layoutWidth;

        context.AddSprite(_fillLeft, left, top, fillColor);

        const int32_t fMidH = _fillMiddle.Height();
        const int32_t fCapW = _fillRight.Width();
        const int32_t fLefW = _fillLeft.Width();
        context.AddSprite(_fillMiddle, left + fLefW, top, (left - fCapW) + barW, top + fMidH, fillColor);

        context.AddSprite(_fillRight, (left - _fillRight.Width()) + barW, top, fillColor);
    }

    if (_children.GetSize() > 0) {
        const Element* child = _children.GetData()[0];
        if (child != nullptr)
            child->Draw(context, alpha);   // slot 6
    }
}

// ??1ProgressElement@UI@@UEAA@XZ   0x180077ef0 — retail's body is a BARE TAIL
// JUMP to ~Element with no restamp: the compiler's chain is all of it.
ProgressElement::~ProgressElement() {}

// ?CreateNewObject@ProgressElement@UI@@SAPEAVBase@Object@@XZ 0x180077e30 --
// `?MemAlloc@MemoryAllocator` (0x1800c7a50) of 0x130, align 8; retail's null
// branch; the ctor. Replaces the factory interpreter row (B39_RULES D2).
Object::Base* ProgressElement::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ProgressElement), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) ProgressElement();
}
// ?CreateInPlaceObject@ProgressElement@UI@@SAPEAVBase@Object@@PEAX@Z 0x180077eb0
Object::Base* ProgressElement::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) ProgressElement();
}

}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 UI-B: the slot doors, the ctor / dtor doors and the
// factory-interpreter row are gone. Their only readers were the minted
// `??_7ProgressElement@UI@@6B@`, its RT_DELDTOR row and FACTUI2: every live
// ProgressElement now wears the compiler's table.
// ============================================================================

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::ProgressElement's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@ProgressElement@UI@@ (0x1801fad28): name "ProgressElement", parent
// "Element" (retail ?_ParentTypeName@ProgressElement@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::ProgressElement, "Element", nullptr)
