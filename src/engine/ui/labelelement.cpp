// ============================================================================
// src/engine/ui/labelelement.cpp — UI::LabelElement, the whole class
// ============================================================================
//
// 2026-09-12, vtable batch 3. `??_7LabelElement@UI@@6B@` (retail 0x180156ad0).
// A `UI::TextElement` subclass that pairs the text with one icon sprite and
// places the two relative to each other.
//
//   slot  method                retail       where
//   ----  --------------------  -----------  ---------------------------------
//    0    vector deleting dtor  0x180042bb0  LabelElement.h (the class's own)
//    1    TypeName              0x1800427f0  here
//    2    Type                  0x1800427d0  here
//    4    ComputeSize           0x1800d2490  here
//    5    ComputeLayout         0x1800d2330  here
//    6    Draw                  0x1800d1c80  here
//   10    SetColor              0x180042970  here
//
// plus ??1LabelElement@UI@@UEAA@XZ 0x1800d25f0 and the factory pair (ENDGAME
// B.3.9 UI-A: the real ctor ??0 0x1800d2620 is in the header; the copy-ctor
// door is retired).
//
// LAYOUT — 0x130 bytes (`MOV EDX,0x130` at 0x180042be2), TextElement's 0x100
// plus twelve dwords. Every one is a constructor store at 0x1800d2620 or a read
// in Draw / ComputeSize:
//
//   +0x100 Sprite  the icon: sheet +0x100, index +0x108 (ctor -1)
//   +0x110 int32   the icon/text pair's INNER alignment, 0..8 (ctor 3)
//   +0x114 uint32  layout mode: 0 icon RIGHT of text, 1 icon LEFT,
//                  2 icon BELOW, 3 icon ABOVE. (ctor 1.)  ComputeSize only
//                  distinguishes `> 1` (stacked) from `<= 1` (side by side).
//   +0x118 int32   minimum text width (ctor 0, via the qword at +0x114)
//   +0x11c int32   the GAP between icon and text, in pixels (ctor 2)
//   +0x120 int32   icon width override,  <1 means "use the sprite" (ctor -1)
//   +0x124 int32   icon height override, <1 means "use the sprite" (ctor -1)
//   +0x128 uint32  the icon's colour — SetColor writes it AND Element::_color
//   +0x12c int32   the measured text width, an OUTPUT of ComputeSize
//
// The TextElement fields this file reads are named at their use: +0xc0 the
// `UI::Font*`, +0xe8/+0xe9 two byte flags, +0xec/+0xf0/+0xf4 three ints and
// +0xf8 the `System::String`.
//
// ENDGAME B.3.9 block UI-A: the class is CONCRETE; the slot doors and the copy
// / ??1 doors (and ui_factory_wave28.cpp's ??0 row), read only by the retired
// minted table, its RT_DELDTOR row and the interpreter, are gone.
//
// ENDGAME B.3.6 block C3: the six slot bodies are `UI::LabelElement` members
// (src/engine/ui/LabelElement.h — the layout above, now asserted; the
// TextElement fields are TextElement's). The registered doors delegate with a
// QUALIFIED call. Font / SpriteContext / Sprite calls are direct member calls.
// ============================================================================

#include "element_common.h"
#include "ui/LabelElement.h"
#include "ui/Font.h"
#include "ui/SpriteContext.h"
#include "system/String.h"

using namespace rtuielem;


namespace UI {

// ??1LabelElement@UI@@UEAA@XZ   0x1800d25f0 -- restamp, then TAIL JMP to
// ~TextElement. ENDGAME B.3.9 UI-A: EMPTY; the compiler's chain.
LabelElement::~LabelElement() {}

// The icon's drawn size: the override when positive, else the sprite's own.
// Both arms test the SAME field — the sprite INDEX — for the "no sprite" case,
// not the sheet pointer.
static int IconWidth(const LabelElement& e) {
    if (e._iconWidth > 0) return e._iconWidth;
    if (e._sprite.spriteIndex < 0) return 0;
    return e._sprite.Width();
}
static int IconHeight(const LabelElement& e) {
    if (e._iconHeight > 0) return e._iconHeight;
    if (e._sprite.spriteIndex < 0) return 0;
    return e._sprite.Height();
}

// ============================================================================
// ?ComputeSize@LabelElement@UI@@UEAAXXZ   0x1800d2490
// ============================================================================
// Measure the text (only when there IS a font AND a non-empty string), floor
// its width by _minTextWidth, then combine with the icon according to the mode:
//
//   mode >  1  (stacked)   width  = max(iconW, textW)
//                          height = gap + iconH + textH
//   mode <= 1  (side)      width  = gap + iconW + textW
//                          height = max(iconH, textH)
//
// `_textWidth` is set to the RAW measured width, BEFORE the floor — the floor
// only affects what is combined with the icon. Draw then reads `_textWidth`, so
// a label whose minimum width exceeds its text draws the text at its natural
// width inside a wider box.
void LabelElement::ComputeSize() {
    if ((_flags & kElementFlagNeedsSizeCalc) == 0) return;

    int textW = 0;
    int textH = 0;
    if (_font != nullptr) {
        char* str = _cachedText._data;
        if (str != nullptr && RtSystem::StringLength(str) > 0) {
            int w = 0, h = 0;
            _font->ComputeStringLayout(_cachedText, _wrap != 0,
                                       _wordWrapSize, _maxWidth, 0,
                                       w, h, nullptr);
            _textWidth = w;
            textH = h;
            textW = w;
            if (w < _minTextWidth) textW = _minTextWidth;
        }
    }

    const int iconW = IconWidth(*this);
    const int iconH = IconHeight(*this);

    if (_mode > 1u) {
        int w = iconW;
        if (w < textW) w = textW;
        _flags &= ~kElementFlagNeedsSizeCalc;
        _layoutWidth  = w;
        _layoutHeight = _gap + iconH + textH;
        return;
    }

    int h = iconH;
    if (h < textH) h = textH;
    _flags &= ~kElementFlagNeedsSizeCalc;
    _layoutWidth  = _gap + iconW + textW;
    _layoutHeight = h;
}

// ============================================================================
// ?ComputeLayout@LabelElement@UI@@UEAAXHHHH@Z   0x1800d2330 — the standard
// Element alignment body (element_common.h), which dispatches slots 5/4.
// ============================================================================
void LabelElement::ComputeLayout(int32_t x, int32_t y, int32_t w, int32_t h) {
    ElementComputeAlignedRect(this, x, y, w, h);
}

// ============================================================================
// ?Draw@LabelElement@UI@@UEBAXAEAVSpriteContext@2@E@Z   0x1800d1c80
// ============================================================================
// Four layout modes, each placing the icon and the text inside the element's
// rectangle and then aligning the pair by `_align`. The four arms share a
// shape but not a body: retail emits them separately, and the differences are
// real — mode 0 aligns the icon on the X axis but pins it to the right edge,
// mode 2 pins it to the bottom, and so on.
//
// TRANSCRIBED LITERALLY, INCLUDING THE COLLAPSED ARMS. Several placements read
// as `((R - (R - w)) >> 1 - (w >> 1)) + (R - w)`, which is algebraically just
// `R - w`; the compiler left the centring expression in because the operands
// were already live. Kept as it is emitted.
//
// THE SHADOW PASS IS A SECOND AddFont AT (+1, +1) IN OPAQUE BLACK (0xff000000)
// drawn BEFORE the real text, gated by `_drawShadow`. Its colour is a CONSTANT.
//
// The text's Y is `(font.lineHeight - font.baseline) + placedY`, i.e. the
// placement computes a top edge and AddFont wants a baseline.
void LabelElement::Draw(SpriteContext& ctx, uint8_t alpha) const {
    if (Element* border = _border)
        border->Draw(ctx, alpha);                            // virtual, slot 6

    const uint32_t baseColor = _color;
    alpha = DimAlpha(this, alpha);

    Font* font = _font;
    char* str  = _cachedText._data;

    int textW = 0, textH = 0;
    bool hasText = false;
    if (font != nullptr && str != nullptr &&
        RtSystem::StringLength(str) >= 1) {
        textW = _textWidth;
        textH = font->_lineHeight;
        hasText = true;
    }

    const int iconW = IconWidth(*this);
    const int iconH = IconHeight(*this);

    const uint32_t mode  = _mode;
    const int      align = _align;

    const int halfTextW = textW >> 1;
    const int halfTextH = textH >> 1;
    const int halfIconW = iconW >> 1;
    const int halfIconH = iconH >> 1;

    const int L = _layoutLeft;
    const int T = _layoutTop;
    const int R = _layoutRight;
    const int B = _layoutBottom;
    const int gap = _gap;

    int textX, textY, iconX, iconY;

    if (mode == 0) {
        // Icon on the RIGHT, text to its left.
        textX = L;
        iconY = T;
        const int textRight = (R - gap) - iconW;
        if (textRight < L + textW) textX = textRight - textW;
        int ty = T;
        if (B < textH + T) ty = B - textH;
        switch (align) {
        case ElementDescription::TopCenter:
        case ElementDescription::MidCenter:
        case ElementDescription::BottomCenter:
            textX = (((textRight - textX) >> 1) - halfTextW) + textX;
            break;
        case ElementDescription::TopRight:
        case ElementDescription::MidRight:
        case ElementDescription::BottomRight:
            textX = textRight - textW;
            break;
        default: break;
        }
        if (align > ElementDescription::TopRight) {
            if (align < ElementDescription::BottomLeft)
                ty = (((B - ty) >> 1) - halfTextH) + ty;
            else if (align <= ElementDescription::BottomRight)
                ty = B - textH;
        }
        textY = ty;
        if (B < iconH + T) iconY = B - iconH;
        iconX = (((R - (R - iconW)) >> 1) - halfIconW) + (R - iconW);
        iconY = (((B - iconY) >> 1) - halfIconH) + iconY;
    } else if (mode == 1) {
        // Icon on the LEFT, text to its right.
        iconX = L;
        iconY = T;
        textX = gap + L + iconW;
        if (R < textX + textW) textX = R - textW;
        int ty = T;
        if (B < T + textH) ty = B - textH;
        switch (align) {
        case ElementDescription::TopCenter:
        case ElementDescription::MidCenter:
        case ElementDescription::BottomCenter:
            textX = (((R - textX) >> 1) - halfTextW) + textX;
            break;
        case ElementDescription::TopRight:
        case ElementDescription::MidRight:
        case ElementDescription::BottomRight:
            textX = R - textW;
            break;
        default: break;
        }
        if (align > ElementDescription::TopRight) {
            if (align < ElementDescription::BottomLeft)
                ty = (((B - ty) >> 1) - halfTextH) + ty;
            else if (align <= ElementDescription::BottomRight)
                ty = B - textH;
        }
        textY = ty;
        if (B < T + iconH) iconY = B - iconH;
        iconY = (((B - iconY) >> 1) - halfIconH) + iconY;
    } else if (mode == 3) {
        // Icon ABOVE, text below it.
        iconX = L;
        iconY = T;
        textX = L;
        int ty = gap + T + iconH;
        if (R < L + textW) textX = R - textW;
        if (B < ty + textH) ty = B - textH;
        switch (align) {
        case ElementDescription::TopCenter:
        case ElementDescription::MidCenter:
        case ElementDescription::BottomCenter:
            textX = (((R - textX) >> 1) - halfTextW) + textX;
            break;
        case ElementDescription::TopRight:
        case ElementDescription::MidRight:
        case ElementDescription::BottomRight:
            textX = R - textW;
            break;
        default: break;
        }
        if (align > ElementDescription::TopRight) {
            if (align < ElementDescription::BottomLeft)
                ty = (((B - ty) >> 1) - halfTextH) + ty;
            else if (align <= ElementDescription::BottomRight)
                ty = B - textH;
        }
        textY = ty;
        if (R < L + iconW) iconX = R - iconW;
        iconX = (((R - iconX) >> 1) - halfIconW) + iconX;
    } else {
        // Mode 2 (and anything else): icon BELOW, text above it.
        iconX = L;
        textX = L;
        int top = T;
        const int textBottom = (B - gap) - iconH;
        if (R < L + textW) textX = R - textW;
        if (textBottom < T + textH) top = textBottom - textH;
        switch (align) {
        case ElementDescription::TopCenter:
        case ElementDescription::MidCenter:
        case ElementDescription::BottomCenter:
            textX = (((R - textX) >> 1) - halfTextW) + textX;
            break;
        case ElementDescription::TopRight:
        case ElementDescription::MidRight:
        case ElementDescription::BottomRight:
            textX = R - textW;
            break;
        default: break;
        }
        int ty = top;
        if (align > ElementDescription::TopRight) {
            if (align < ElementDescription::BottomLeft)
                ty = (((textBottom - top) >> 1) - halfTextH) + top;
            else if (align <= ElementDescription::BottomRight)
                ty = textBottom - textH;
        }
        textY = ty;
        if (R < L + iconW) iconX = R - iconW;
        iconX = (((R - iconX) >> 1) - halfIconW) + iconX;
        iconY = (((B - (B - iconH)) >> 1) - halfIconH) + (B - iconH);
    }

    ctx.AddSprite(_sprite, iconX, iconY, iconX + iconW, iconH + iconY, _iconColor);

    if (!hasText) return;

    const int baselineAdj = font->_lineHeight - font->_baseLine;

    if (_drawShadow != 0) {
        ctx.AddFont(Object::ExternalPtr<Font>(font), _cachedText, _wrap != 0,
                    _wordWrapSize, _justify, _maxWidth,
                    textX + 1, baselineAdj + 1 + textY, System::kColorBlack);
    }
    ctx.AddFont(Object::ExternalPtr<Font>(font), _cachedText, _wrap != 0,
                _wordWrapSize, _justify, _maxWidth,
                textX, baselineAdj + textY,
                rtuielem::ComputeColor(baseColor, alpha));   // the inlined helper,
                                                             // not Element's member
}

// ============================================================================
// ?SetColor@LabelElement@UI@@UEAAXI@Z   0x180042970
// ============================================================================
// Two stores and no dirty walk: the icon colour AND `Element::_color`. It does
// NOT call `Element::SetColor` — it writes +0x38 itself.
void LabelElement::SetColor(uint32_t color) {
    _iconColor = color;
    _color     = color;
}

// ?CreateNewObject@LabelElement@UI@@SAPEAVBase@Object@@XZ  0x1800428e0
// The factory-interpreter row this replaces: MemAlloc(0x130, 8), retail's
// null branch, then the out-of-line ctor ??0LabelElement (0x1800d2620).
Object::Base* LabelElement::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(LabelElement), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) LabelElement();
}
// ?CreateInPlaceObject@LabelElement@UI@@SAPEAVBase@Object@@PEAX@Z  0x180042960
Object::Base* LabelElement::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) LabelElement();
}
}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::LabelElement's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@LabelElement@UI@@ (0x1801fa040): name "LabelElement", parent
// "TextElement" (retail ?_ParentTypeName@LabelElement@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::LabelElement, "TextElement", nullptr)
