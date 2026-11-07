// ============================================================================
// src/engine/ui/textelement.cpp — UI::TextElement's vtable slots + dtor
// ============================================================================
//
// `??_7TextElement@UI@@6B@` (retail 0x180156b80):
//
//   0  vector deleting dtor  0x1800422f0  -> TextElement.h (the class's own)
//   1  TypeName              0x180041ef0  here
//   2  Type                  0x180041ed0  here
//   4  ComputeSize           0x18011f060  here
//   5  ComputeLayout         0x18011f120  here
//   6  Draw                  0x18011f280  here
//
// plus ??1TextElement@UI@@UEAA@XZ (0x18011eda0) and the factory pair, here.
//
// ENDGAME B.3.6 block C3: the five slot bodies are `UI::TextElement` members
// (src/engine/ui/TextElement.h carries the layout, pinned from the ctor
// 0x18011eb80). The registered doors delegate with a QUALIFIED call. Calls onto
// `UI::Font` and `UI::SpriteContext` are direct member calls now (block D made
// both real classes); the border and slot-4 dispatches are C++ virtual calls
// (they read the object's MINTED table, exactly as retail's `CALL [RAX+..]`).
//
// ENDGAME B.3.9 block UI-A (B39_RULES D1/D2): the class is CONCRETE; the real
// ctor `??0TextElement@UI@@QEAA@XZ` (0x18011eb80) is in the header, and the
// slot doors, the ??1 door and element_bodies_b5.cpp's ??0 door, read only by
// the retired minted table, its RT_DELDTOR row and the interpreter rows, are
// gone.
//
// NOT HERE, and deliberately:
//   * `?CacheText@` / `?SetParameterCount@` — textelement_cachetext.cpp.
//   * `?SetParameter@` / `?SetText@` — widgets_layout.cpp's doors (fragment pass).
//
// ~TextElement releases the cached string at +0xF8 and then destroys the
// parameter array at +0xC8 through 0x180010ab0 — the `~Array` variant that
// destroys ELEMENTS first (it calls 0x180011010 over {data,size} before
// freeing the buffer), which is right because the elements are System::Strings.
// That address is already registered by src/engine/collection/array_destroy.cpp.
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "element_common.h"   // rtuielem::AtomicExport / ComputeColor, ResolveAddr
#include "ui/TextElement.h"
#include "ui/Font.h"
#include "ui/SpriteContext.h"
#include "system/String.h"


namespace UI {

// ??1TextElement@UI@@UEAA@XZ   0x18011eda0 — retail's body, moved here from the
// retired door (ENDGAME B.3.9 UI-A). The entry restamp is the compiler's. The
// cached text at +0xf8 is released through System::String's own Release
// (retail's asymmetric idiom: `data - 8` freed on the last reference, the field
// NULLED otherwise); the parameter Strings through 0x180010ab0, the
// element-destroying ~Array (`_params` is a plain three-field header, so no
// member ~Array follows); then the compiler chains ~Element (0x1801161a0).
TextElement::~TextElement() {
    // B4 X7-RT: empty. The release of +0xf8 (0x18011edc0..0x18011edeb) and
    // ~Array<String> on +0xc8 (0x18011edfd CALL 0x180010ab0) are the two typed
    // members' destructors, which the compiler runs after this body in that
    // order (reverse declaration order), then ~Element (0x18011ee0b).
}

// slot 4 — 0x18011f060
void TextElement::ComputeSize() {
    if ((_flags & kElementFlagNeedsSizeCalc) == 0) return;

    _font->ComputeStringLayout(_cachedText, _wrap != 0, _wordWrapSize,
                               _maxWidth, _justify, _layoutWidth, _layoutHeight,
                               nullptr);

    if (_layoutWidth  < _minWidth)  _layoutWidth  = _minWidth;
    if (_layoutHeight < _minHeight) _layoutHeight = _minHeight;

    const int32_t cap = _maxWidth;
    if (cap > 0 && cap < _layoutWidth) _layoutWidth = cap;

    _flags &= ~kElementFlagNeedsSizeCalc;
}

// slot 5 — 0x18011f120
void TextElement::ComputeLayout(int32_t x, int32_t y, int32_t right, int32_t bottom) {
    if (Element* border = _border)
        border->ComputeLayout(x, y, right, bottom);            // virtual, slot 5

    ComputeSize();                                             // virtual, slot 4

    int32_t w = _layoutWidth;
    int32_t h = _layoutHeight;
    const int32_t align = _alignment;

    if (right  < w + x) x = right  - w;
    if (bottom < h + y) y = bottom - h;

    int32_t px = x + _leftPad;
    int32_t py = y + _topPad;
    const int32_t aw = (right  - _rightPad)  - px;
    const int32_t ah = (bottom - _bottomPad) - py;

    if ((_flags & kElementFlagAlignArg1) != 0) w = aw;
    if ((_flags & kElementFlagAlignArg2) != 0) h = ah;

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

// slot 6 — 0x18011f280
void TextElement::Draw(SpriteContext& ctx, uint8_t alpha) const {
    if (Element* border = _border)
        border->Draw(ctx, alpha);                              // virtual, slot 6

    const uint32_t raw = _color;
    if ((_flags & kElementFlagDimColor) != 0) alpha >>= 1;

    Font* font = _font;
    const int32_t baseline = font->_lineHeight - font->_baseLine;

    if (_drawShadow != 0) {
        ctx.AddFont(Object::ExternalPtr<Font>(font), _cachedText, _wrap != 0,
                    _wordWrapSize, _justify, _maxWidth,
                    _layoutLeft + 1, _layoutTop + 1 + baseline,
                    System::kColorBlack);
    }

    ctx.AddFont(Object::ExternalPtr<Font>(font), _cachedText, _wrap != 0,
                _wordWrapSize, _justify, _maxWidth,
                _layoutLeft, baseline + _layoutTop,
                rtuielem::ComputeColor(raw, alpha));
}

// ?CreateNewObject@TextElement@UI@@SAPEAVBase@Object@@XZ  0x180041fe0
// The factory-interpreter row this replaces: MemAlloc(0x100, 8), retail's
// null branch, then the out-of-line ctor ??0TextElement (0x18011eb80).
Object::Base* TextElement::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TextElement), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) TextElement();
}
// ?CreateInPlaceObject@TextElement@UI@@SAPEAVBase@Object@@PEAX@Z  0x180042060
Object::Base* TextElement::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) TextElement();
}
}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::TextElement's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@TextElement@UI@@ (0x1801faee8): name "TextElement", parent
// "Element" (retail ?_ParentTypeName@TextElement@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::TextElement, "Element", nullptr)
