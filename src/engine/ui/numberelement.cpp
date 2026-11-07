// ============================================================================
// src/engine/ui/numberelement.cpp — UI::NumberElement, the whole class
// ============================================================================
//
// `??_7NumberElement@UI@@6B@` (retail 0x1801574d0):
//
//   0  vector deleting dtor  0x18004ab80  -> NumberElement.h (the class's own)
//   1  TypeName              0x18004a820  here
//   2  Type                  0x18004a800  here
//   4  ComputeSize           0x180119000  here
//   5  ComputeLayout         0x180119080  here
//   6  Draw                  0x1801191e0  here
//
// plus ??0NumberElement (0x180118c70), ??1NumberElement (0x180118ce0) and the
// private `?CacheText@NumberElement@UI@@AEAAXXZ` (0x1801189f0) — which IS this
// file's, unlike TextElement's same-named 0x18011ec10.
//
// The layout is src/engine/ui/NumberElement.h and is complete; this port is
// what let that header's struct catch up with its own banner.
//
// NOT HERE: `?ComputeLineLayout@Font@UI@@` (0x180116c70) and
// `?AddFont@SpriteContext@UI@@` (0x18011ccb0), both on the UI non-slot callee
// list; reached by address.
//
// ENDGAME B.3.6 block C3: the five slot bodies and CacheText are
// `UI::NumberElement` members. ENDGAME B.3.9 UI-A: the class is concrete (the
// real ctor ??0 0x180118c70 is in the header and calls CacheText itself); the
// slot and ctor/dtor doors are retired, CacheText's door stays. Font /
// SpriteContext calls are direct.
// ============================================================================


#include "element_common.h"
#include "ui/NumberElement.h"
#include "ui/Font.h"
#include "ui/SpriteContext.h"
#include "system/String.h"

using namespace rtuielem;

namespace {


void MemFree(void* p) { System::MemoryAllocator::MemFree(p); }

// The inlined String::operator= and the two releases, retail's asymmetric
// shape (see widget_common.h, which these mirror).
void AssignString(char*& field, char* nv) {
    char* old = field;
    if (old == nv) return;
    if (old != nullptr) {
        int32_t* rc = &RtSystem::StringRefCount(old);
        if (--*rc == 0) {
            field = field - 8;
            MemFree(field);
        } else {
            field = nullptr;
        }
    }
    field = nv;
    if (nv != nullptr) ++RtSystem::StringRefCount(nv);
}
void ReleaseStringLocal(char*& slot) {
    char* p = slot;
    if (p == nullptr) return;
    int32_t* rc = &RtSystem::StringRefCount(p);
    if (--*rc == 0) { slot = p - 8; MemFree(slot); }
}

}  // namespace

namespace UI {

// ??1NumberElement@UI@@UEAA@XZ 0x180118ce0 — retail's body, moved here from
// the retired door (ENDGAME B.3.9 UI-A): release the cached text
// (0x180118d00..0x180118d36); the restamp and the chain into ~Element are the
// compiler's. B4 X-RH: the release is the System::String member's destructor,
// run after this empty body and before ~Element: retail's order.
NumberElement::~NumberElement() {
}

// ---------------------------------------------------------------------------
// ?CacheText@NumberElement@UI@@AEAAXXZ   0x1801189f0
//
// The four format literals live at 0x180159144 / 0x180154848 / 0x180159148 /
// 0x180159150 and are reproduced as string literals — a rodata literal is the
// one kind of constant that CAN be retyped, because its bytes are its value.
// ---------------------------------------------------------------------------
void NumberElement::CacheText() {
    char* tmp = nullptr;
    if (_mode == 0) {
        const char* fmt = (_showSign != 0) ? "%+i" : "%i";
        // `?Format@String@System@@SA?AV12@PEBDZZ` (0x1800c9730): retslot = `tmp`.
        new (static_cast<void*>(&tmp)) System::String(System::String::Format(fmt, _intValue));
        AssignString(_cachedText._data, tmp);
    } else {
        const char* fmt = (_showSign != 0) ? "%+.2f" : "%.2f";
        new (static_cast<void*>(&tmp)) System::String(
            System::String::Format(fmt, static_cast<double>(_floatValue)));
        AssignString(_cachedText._data, tmp);
    }
    ReleaseStringLocal(tmp);
}

// slot 4 — 0x180119000
void NumberElement::ComputeSize() {
    if ((_flags & kElementFlagNeedsSizeCalc) == 0) return;

    Font* font = _font;
    _layoutWidth  = 0;      // one qword store clears both
    _layoutHeight = 0;

    const char* text = _cachedText._data;
    int32_t len = 0;
    if (text != nullptr)
        len = RtSystem::StringLength(text);                    // System::String length

    _layoutWidth  = font->ComputeLineLayout(text, len, 0, 0, -1, 0, nullptr);
    _layoutHeight = font->_lineHeight;
    _flags &= ~kElementFlagNeedsSizeCalc;
}

// slot 5 — 0x180119080
void NumberElement::ComputeLayout(int32_t x, int32_t y, int32_t right, int32_t bottom) {
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

// slot 6 — 0x1801191e0
void NumberElement::Draw(SpriteContext& ctx, uint8_t alpha) const {
    if (Element* border = _border)
        border->Draw(ctx, alpha);                              // virtual, slot 6

    const uint32_t flags = _flags;
    // `(flags & 0x1800000) == 0 || (flags >> 0x18 & 1) != 0` — draw when the
    // element is fully enabled, OR when it is SYSTEM-disabled (bit 24), which
    // still paints; a plain Disabled (bit 23) alone suppresses it.
    if ((flags & kElementFlagAnyDisabled) != 0 && (flags & kElementFlagSystemDisabled) == 0) return;

    const uint32_t color = _color;
    if ((flags & kElementFlagDimColor) != 0) alpha >>= 1;

    Font* font = _font;
    ctx.AddFont(Object::ExternalPtr<Font>(font),
                _cachedText,
                false, 0, 0, -1,
                _layoutLeft,
                (font->_lineHeight - font->_baseLine) + _layoutTop,
                rtuielem::ComputeColor(color, alpha));
}

}  // namespace UI

namespace UI {
// ?CreateNewObject@NumberElement@UI@@SAPEAVBase@Object@@XZ  0x18004a910 — the
// FACTUI2 row this replaces: MemAlloc(0xe8, 8), retail's null branch, then the
// out-of-line ctor ??0NumberElement (0x180118c70).
Object::Base* NumberElement::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(NumberElement), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) NumberElement();
}
// ?CreateInPlaceObject@NumberElement@UI@@SAPEAVBase@Object@@PEAX@Z  0x18004a990
Object::Base* NumberElement::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) NumberElement();
}
}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::NumberElement's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@NumberElement@UI@@ (0x1801facb8): name "NumberElement", parent
// "Element" (retail ?_ParentTypeName@NumberElement@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::NumberElement, "Element", nullptr)
