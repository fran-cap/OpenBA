// ============================================================================
// src/engine/ui/borderelement.cpp — UI::BorderElement, the whole class
// ============================================================================
//
// 2026-09-12, vtable batch 3. `??_7BorderElement@UI@@6B@`, six owned rows:
//
//   slot  method                retail       where
//   ----  --------------------  -----------  ---------------------------------
//    0    vector deleting dtor  0x1800414f0  BorderElement.h (the class's own)
//    1    TypeName              0x1800410b0  here
//    2    Type                  0x180041090  here
//    4    ComputeSize           0x1801101d0  here
//    5    ComputeLayout         0x180110360  here
//    6    Draw                  0x1801103e0  here
//   (3, 7..11 are UI::Element's own bodies, already ported in element*.cpp)
//
// plus ??1BorderElement@UI@@UEAA@XZ 0x180041230 and the factory pair.
//
// ENDGAME B.3.6 block C1: the five slot bodies are MEMBERS of
// `UI::BorderElement` (src/engine/ui/BorderElement.h, which now owns the
// nine-slice layout — nine UI::Sprite at +0xc0..+0x14f, row major — and its
// evidence). ENDGAME B.3.9 block UI-A: the class is concrete (its real ctor
// `??0BorderElement@UI@@QEAA@XZ` 0x18010fd40 is in the header); the slot
// doors, the copy-ctor door and the ??1 door, read only by the retired minted
// table / RT_DELDTOR row, are gone.
// ============================================================================
#include "element_common.h"
#include "ui/BorderElement.h"
#include "ui/SpriteContext.h"

using namespace rtuielem;

namespace {

// UI::SpriteContext (block D's class), reached by address.

}  // namespace

namespace UI {

// ============================================================================
// ?ComputeSize@BorderElement@UI@@UEAAXXZ   0x1801101d0
// ============================================================================
// The minimum box the frame needs: the wider of the two corner PAIRS on each
// axis. Only the FOUR CORNERS participate — the stretchable edges contribute
// nothing to the minimum, which is why a border with only edge sprites
// measures 0x0.
//
//   width  = max(bottomLeft.w + bottomRight.w, topRight.w + topLeft.w)
//   height = max(bottomRight.h + topRight.h,   bottomLeft.h + topLeft.h)
//
// *** THE TWO PAIRINGS ARE NOT THE OBVIOUS ONES *** — the height compares a
// RIGHT-column pair against a LEFT-column pair while the width compares a
// BOTTOM-row pair against a mixed pair. That is what 0x180110276..0x1801102e2
// computes and it is transcribed rather than normalised.
void BorderElement::ComputeSize() {
    if ((_flags & kElementFlagNeedsSizeCalc) == 0) return;

    const int tlW = SpriteW(_topLeft);
    const int trW = SpriteW(_topRight);
    const int blW = SpriteW(_bottomLeft);
    const int brW = SpriteW(_bottomRight);

    const int tlH = SpriteH(_topLeft);
    const int blH = SpriteH(_bottomLeft);
    const int trH = SpriteH(_topRight);
    const int brH = SpriteH(_bottomRight);

    int w = blW + brW;
    if (w < trW + tlW) w = trW + tlW;

    int h = brH + trH;
    if (h < blH + tlH) h = blH + tlH;

    _layoutWidth = w;
    _flags &= ~UI::kElementFlagNeedsSizeCalc;
    _layoutHeight = h;
}

// ============================================================================
// ?ComputeLayout@BorderElement@UI@@UEAAXHHHH@Z   0x180110360
// ============================================================================
// NOT the standard aligned body: a border always fills the box it is given,
// inset by its own padding. No ComputeSize call, no alignment, no clamping.
void BorderElement::ComputeLayout(int32_t x, int32_t y, int32_t w, int32_t h) {
    Element* border = _border;
    if (border != nullptr) border->ComputeLayout(x, y, w, h);

    _layoutRight  = w - _rightPad;
    _layoutLeft   = _leftPad + x;
    _layoutBottom = h - _bottomPad;
    _layoutTop    = _topPad + y;
}

// ============================================================================
// ?Draw@BorderElement@UI@@UEBAXAEAVSpriteContext@2@E@Z   0x1801103e0
// ============================================================================
// The nine-slice, in retail's order: top-left corner, top edge, top-right
// corner, left edge, centre, right edge, bottom-left corner, bottom edge,
// bottom-right corner. Corners go through the POINT form of AddSprite
// (0x18011cf00, drawn at natural size); edges and centre through the RECT form
// (0x18011d3c0, stretched).
//
// Every extent is recomputed from the sprite sheet at its point of use — retail
// re-reads e.g. `SpriteH(bottomLeft)` in three separate places rather than
// caching it, and the reads are interleaved with the calls. The calls can
// change a sprite sheet (they do not today), so the re-reads are kept.
void BorderElement::Draw(SpriteContext& context, uint8_t alpha) const {
    const Element* border = _border;
    if (border != nullptr) border->Draw(context, alpha);

    const uint32_t base = _color;
    alpha = DimAlpha(this, alpha);
    const uint32_t c = rtuielem::ComputeColor(base, alpha);

    auto L = [&] { return _layoutLeft; };
    auto T = [&] { return _layoutTop; };
    auto R = [&] { return _layoutRight; };
    auto B = [&] { return _layoutBottom; };

    // ?AddSprite@SpriteContext@UI@@ point (0x18011cf00) and rect (0x18011d3c0).
    context.AddSprite(_topLeft, L(), T(), c);                                         // 180110469

    context.AddSprite(_top,                                                         // 18011050d
         L() + SpriteW(_topLeft), T(),
         R() - SpriteW(_topRight), T() + SpriteH(_top), c);

    context.AddSprite(_topRight, R() - SpriteW(_topRight), T(), c);                  // 180110545

    context.AddSprite(_left,                                                        // 1801106b8
         L(), T() + SpriteH(_topLeft),
         L() + SpriteW(_left), B() - SpriteH(_bottomLeft), c);

    context.AddSprite(_centre,                                                      // 1801106b8 (2nd)
         L() + SpriteW(_left), T() + SpriteH(_top),
         R() - SpriteW(_right), B() - SpriteH(_bottom), c);

    context.AddSprite(_right,                                                       // 18011074e
         R() - SpriteW(_right), T() + SpriteH(_topRight),
         R(), B() - SpriteH(_bottomLeft), c);

    context.AddSprite(_bottomLeft, L(), B() - SpriteH(_bottomLeft), c);              // 180110786

    context.AddSprite(_bottom,                                                      // 180110826
         L() + SpriteW(_bottomLeft), B() - SpriteH(_bottom),
         R() - SpriteW(_bottomRight), B(), c);

    context.AddSprite(_bottomRight,                                                   // 180110877
       R() - SpriteW(_bottomRight), B() - SpriteH(_bottomRight), c);
}

}  // namespace UI

// ============================================================================
// ??1BorderElement@UI@@UEAA@XZ   0x180041230
// ============================================================================
// *** IT DOES NOT RE-STAMP ITS OWN VTABLE. *** Five instructions, all of them
// frame setup, then `JMP ~Element`: the body is EMPTY and the compiler chains
// ~Element (ENDGAME B.3.9 UI-A; the retired door called it by address).
namespace UI {
BorderElement::~BorderElement() {}

// ?CreateNewObject@BorderElement@UI@@SAPEAVBase@Object@@XZ  0x1800411a0
// The FACTUI1 row this replaces: `?MemAlloc@MemoryAllocator@System@@`
// (0x1800c7a50) of 0x150 bytes, align 8; retail's null branch; then the ctor.
Object::Base* BorderElement::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(BorderElement), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) BorderElement();
}
// ?CreateInPlaceObject@BorderElement@UI@@SAPEAVBase@Object@@PEAX@Z  0x180041220
Object::Base* BorderElement::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) BorderElement();
}
}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::BorderElement's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@BorderElement@UI@@ (0x1801fa8c8): name "BorderElement", parent
// "Element" (retail ?_ParentTypeName@BorderElement@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::BorderElement, "Element", nullptr)
