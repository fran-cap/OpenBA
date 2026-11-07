// src/game/Component/captureboxelement.cpp
// CaptureBoxElement — the rubber-band box of the debug SpriteCaptureUI (a UI::Element).
//
// CaptureBoxElement (size 0xe0) extends UI::Element. The exe factory
// (0x140013eb0): MemAlloc(0xe0,8) -> UI::Element::Element -> install
// vtable_CaptureBoxElement (0x14015d440) -> UI::Sprite::Sprite(this+0xc0)
// (default ctor) -> *(this+0xd0) = 0, *(this+0xd8) = 0.
//
// ENDGAME B.3.8 VIS: a REAL class (CaptureBoxElement.h) with the compiler's
// 12-slot table and retail's slots 4/5/6. Before, the tree's "dormant-safe"
// factory built the object through the ??0Element DOOR and kept the minted
// UI::Element table — slot 4 (ComputeSize) was _purecall there, slots 1/2
// answered Element, and nothing drew the box.

// graphics/Camera.h FIRST (via video/Context.h): src/engine/volume/Volume.h's
// global-scope `struct Rectangle` is hidden by <wingdi.h>'s `Rectangle()` once
// <windows.h> has been seen.
#include "video/Context.h"
#include "ui/Element.h"
#include "ui/Sprite.h"
#include "ui/SpriteContext.h"
#include "object/Internal.h"   // Object::detail::InternalMemAlloc / InternalMemFree
#include "Object/factory_registration.h"
#include "Component/CaptureBoxElement.h"
#include <cstdint>
#include <new>

// 0x140079040 / create 0x140013eb0: Element() is `??0Element@UI@@`; the
// 0x14015d440 stamp is the compiler's; Sprite() on +0xc0; +0xd0 = +0xd8 = 0.
CaptureBoxElement::CaptureBoxElement()
    : _lineSprite(), _x0(0), _y0(0), _x1(0), _y1(0) {}

// [4] 0x140078fb0 — ComputeSize: if the "size-pending" flag (bit 27 of +0x34)
// is set, the element's size (+0x60/+0x64) is 1x1 and the flag is cleared.
void CaptureBoxElement::ComputeSize() {
    uint32_t flags = _flags;
    if (flags & UI::kElementFlagNeedsSizeCalc) {
        _layoutWidth = 1;
        _layoutHeight = 1;
        _flags = flags & ~UI::kElementFlagNeedsSizeCalc;
    }
}

// [5] 0x140078ee0 — ComputeLayout: the base, then the aligned screen rect into
// +0x68 (left) / +0x6c (top) / +0x70 (right) / +0x74 (bottom).
void CaptureBoxElement::ComputeLayout(int32_t x, int32_t y, int32_t w, int32_t h) {
    UI::Element::ComputeLayout(x, y, w, h);   // base — QUALIFIED (slot 5)
    int outX = 0, outY = 0, outW = 0, outH = 0;
    GetAlignedPosition(x, y, w, h, 0, 0, outX, outY, outW, outH, false, false);
    _layoutLeft   = outX;
    _layoutRight  = outX + outW;
    _layoutTop    = outY;
    _layoutBottom = outY + outH;
}

// [6] 0x140078da0 — Draw: the base, then the four sides of the box
// (+0xd0,+0xd4)-(+0xd8,+0xdc) in the element color (+0x38) modulated by alpha
// (halved if flag bit 18 of +0x34 is set).
void CaptureBoxElement::Draw(UI::SpriteContext& context, uint8_t alpha) const {
    UI::Element::Draw(context, alpha);   // base — QUALIFIED (slot 6)
    uint32_t a32 = alpha;
    if (_flags & UI::kElementFlagDimColor) a32 = (uint32_t)(alpha >> 1);
    uint32_t v = _color;
    uint32_t color = ((((v >> 8) & 0xffff0000u) * a32 ^ v) & 0xffffffu)
                     ^ (((v >> 8) & 0xff0000u) * a32);
    context.AddLine(_lineSprite, _x0, _y0, _x1, _y0, color);
    context.AddLine(_lineSprite, _x0, _y1, _x1, _y1, color);
    context.AddLine(_lineSprite, _x0, _y0, _x0, _y1, color);
    context.AddLine(_lineSprite, _x1, _y0, _x1, _y1, color);
}

// create 0x140013eb0: MemAlloc(0xe0, 8), then the (inlined) ctor.
::Object::Base* CaptureBoxElement::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(CaptureBoxElement), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) CaptureBoxElement());
}
// createInPlace 0x140013f20: the same construction on `storage`.
::Object::Base* CaptureBoxElement::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) CaptureBoxElement());
}

DEFINE_EXE_TYPE(CaptureBoxElement, "Element")
