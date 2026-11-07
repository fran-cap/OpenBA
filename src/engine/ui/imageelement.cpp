// ============================================================================
// src/engine/ui/imageelement.cpp — UI::ImageElement, the whole class
// ============================================================================
//
// 2026-09-12, vtable batch 3 (UI::*Element, first half). `??_7ImageElement@UI@@6B@`
// (retail 0x1801560b0) is twelve slots:
//
//   slot  method                       retail       where
//   ----  ---------------------------  -----------  --------------------------
//    0    vector deleting dtor         0x180036600  ImageElement.h (the class's own)
//    1    TypeName                     0x1800362b0  here
//    2    Type                         0x180036290  here
//    3    Element::Persist             0x180116870  element_vtable.cpp
//    4    ComputeSize                  0x180118650  here
//    5    ComputeLayout                0x1801186f0  here
//    6    Draw                         0x180118850  here
//    7..11                             Element's
//
// plus ??1ImageElement@UI@@UEAA@XZ 0x180118470 and the factory pair (ENDGAME
// B.3.9 UI-A: the real ctor ??0 0x180118410 is in the header; the copy-ctor
// door is retired).
//
// Layout: src/engine/ui/ImageElement.h (0xE0 = UI::Element 0xC0 + a
// UI::Sprite at +0xC0, a float rotation at +0xD0 and the width/height
// overrides at +0xD4 / +0xD8).
//
// SLOTS 4/5/6 ARE ALSO ANIMATEDIMAGEELEMENT'S. `??_7AnimatedImageElement@UI@@6B@`
// repeats these three addresses (the subclass overrides only Update), so they
// are registered ONCE, here; in C++ the subclass simply does not override them.
//
// `GetSprite` / `SetRotation` / `SetSprite` / `SetWidthHeight` are
// widgets_layout.cpp's doors (fragment pass).
//
// ENDGAME B.3.6 block C2: slots 1/2/4/5/6 are MEMBERS of `UI::ImageElement`;
// the registered doors are one-line QUALIFIED delegations. The border's raw
// slot-6 read is a virtual member call; the rotated AddSprite (by address
// 0x18011cb10 before) is the real `UI::SpriteContext` member.
//
// ENDGAME B.3.9 block UI-A: the class is CONCRETE; the slot doors and the
// copy / ??1 doors, read only by the retired minted tables and RT_DELDTOR row,
// are gone (slots 4/5/6 were also AnimatedImageElement's).
// ============================================================================
#include "element_common.h"
#include "ui/ImageElement.h"
#include "ui/SpriteContext.h"

using namespace rtuielem;

namespace {

}  // namespace

namespace UI {

// ============================================================================
// ?ComputeSize@ImageElement@UI@@UEAAXXZ   0x180118650
// ============================================================================
// Early-out on flag bit 27, then: width  = _width  > 0 ? _width  : sprite width,
//                                 height = _height > 0 ? _height : sprite height.
//
// *** THE EARLY RETURN IN THE HEIGHT ARM IS RETAIL'S, NOT A TIDY-UP. *** When
// the height override is <= 0 AND the sprite index is negative, retail clears
// bit 27, writes ZERO to +0x64 and RETURNS — the flag store happens BEFORE the
// height store on that path and AFTER it on the others, so the two arms are
// kept separate here.
//
// `flags` is snapshotted once and the clear is written from the snapshot.
void ImageElement::ComputeSize() {
    const uint32_t flags = _flags;
    if ((flags & kElementFlagNeedsSizeCalc) == 0) return;

    int w = _width;
    if (w < 1) w = SpriteW(_sprite);
    _layoutWidth = w;

    int h = _height;
    if (h < 1) {
        if (_sprite.spriteIndex < 0) {
            _flags = flags & ~static_cast<uint32_t>(kElementFlagNeedsSizeCalc);
            _layoutHeight = 0;
            return;
        }
        h = SpriteH(_sprite);
    }
    _layoutHeight = h;
    _flags = flags & ~static_cast<uint32_t>(kElementFlagNeedsSizeCalc);
}

// ============================================================================
// ?ComputeLayout@ImageElement@UI@@UEAAXHHHH@Z   0x1801186f0
// ============================================================================
// The standard body, transcribed once in element_common.h.
void ImageElement::ComputeLayout(int32_t x, int32_t y, int32_t w, int32_t h) {
    ElementComputeAlignedRect(this, x, y, w, h);
}

// ============================================================================
// ?Draw@ImageElement@UI@@UEBAXAEAVSpriteContext@2@E@Z   0x180118850
// ============================================================================
// Border first (through ITS slot 6), the dim-alpha halve, then one rotated
// AddSprite (0x18011cb10) covering the whole layout rect.
//
// The rotation is loaded into XMM0 and spilled to the outgoing float slot
// BEFORE the colour maths runs (0x18011888b / 0x18011889d); that ordering is
// invisible and is not reproduced.
void ImageElement::Draw(SpriteContext& context, uint8_t alpha) const {
    const Element* border = _border;
    if (border != nullptr)
        border->Draw(context, alpha);

    const uint32_t color = _color;
    alpha = DimAlpha(this, alpha);

    context.AddSprite(_sprite,
                      _layoutLeft,  _layoutTop,
                      _layoutRight, _layoutBottom,
                      rtuielem::ComputeColor(color, alpha),
                      _rotation);
}

}  // namespace UI

// ============================================================================
// ??1ImageElement@UI@@UEAA@XZ   0x180118470 -- restamp, then ~Element.
// ENDGAME B.3.9 UI-A: EMPTY; the compiler restamps and chains ~Element. The
// class owns no refcounted member: the Sprite is two plain words.
// ============================================================================
namespace UI {
ImageElement::~ImageElement() {}
}  // namespace UI

namespace UI {
// ?CreateNewObject@ImageElement@UI@@SAPEAVBase@Object@@XZ  0x1800363a0
// The factory-interpreter row this replaces: MemAlloc(0xe0, 8), retail's
// null branch, then the ctor's stores (ui_factory_wave28.cpp's retired row).
Object::Base* ImageElement::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ImageElement), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) ImageElement();
}
// ?CreateInPlaceObject@ImageElement@UI@@SAPEAVBase@Object@@PEAX@Z  0x180036450
Object::Base* ImageElement::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) ImageElement();
}
}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::ImageElement's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@ImageElement@UI@@ (0x1801fac48): name "ImageElement", parent
// "Element" (retail ?_ParentTypeName@ImageElement@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::ImageElement, "Element", nullptr)
