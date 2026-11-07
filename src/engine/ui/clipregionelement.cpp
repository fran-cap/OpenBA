// ============================================================================
// src/engine/ui/clipregionelement.cpp — UI::ClipRegionElement, the whole class
// ============================================================================
//
// 2026-09-12, vtable batch 3. `??_7ClipRegionElement@UI@@6B@` (retail
// 0x1801562f0). The scrolling viewport: a fixed-size window onto one child that
// may be larger than it, with a scissor rectangle around the drawing and a
// scroll offset subtracted from the child's layout.
//
//   slot  method                retail       where
//   ----  --------------------  -----------  ---------------------------------
//    0    vector deleting dtor  0x180038370  ClipRegionElement.h (the class's own)
//    1    TypeName              0x180037f70  here
//    2    Type                  0x180037f50  here
//    4    ComputeSize           0x1800cbc60  here
//    5    ComputeLayout         0x1800cba00  here
//    6    Draw                  0x1800cb900  here
//    7    Update                0x1800cbd70  here
//    9    Contains              0x1800cbcc0  here
//
// plus ??1ClipRegionElement@UI@@UEAA@XZ 0x1800cbe30 and the factory pair.
//
// ENDGAME B.3.6 block C1: the seven slot bodies are MEMBERS of
// `UI::ClipRegionElement` (src/engine/ui/ClipRegionElement.h, which now
// owns the six-dword layout and its evidence). The base calls in Update and
// Contains are QUALIFIED member calls (they were `ResolveAddr` by address).
// ENDGAME B.3.9 UI-A: the class is concrete (real ctor ??0 0x1800cbe60 in the
// header); the slot, ctor, copy-ctor and dtor doors are retired.
// ============================================================================
#include "element_common.h"
#include "ui/ClipRegionElement.h"
// ENDGAME B.3.5 block VC: `Video::Viewport`, which replaces this file's local
// duplicate of its four fields (RULE B5-2's duplicate-view collapse).
#include "video/Context.h"
#include "ui/SpriteContext.h"

using namespace rtuielem;

namespace {

// UI::UserInterface's current clip rectangle, four dwords at +0xfc..+0x108
// (block E's class, reached at raw offsets). Proven by this class: Update and
// Contains save it, overwrite it with their own layout rect, run the base body,
// and put the old one back.

// UI::SpriteContext::SetScissor(ctx, Viewport& out, const Viewport& in) —
// returns the PREVIOUS scissor by value through the hidden slot in RDX.
// Block D's class, reached by address.

// ENDGAME B.3.5 block VC: the REAL `Video::Viewport` (src/engine/video/
// Context.h), field-identical to the local duplicate this file once had.
using Viewport = ::Video::Viewport;

}  // namespace

namespace UI {

// ============================================================================
// ?ComputeSize@ClipRegionElement@UI@@UEAAXXZ   0x1800cbc60
// ============================================================================
// The viewport is whatever size it was told to be; the CHILD's padded size is
// measured into +0xc8/+0xcc for the scroll bars to read.
//
// `_children[0]` is dereferenced with NO null test (0x1800cbc7a loads it and
// passes it straight to GetPaddedSize). A ClipRegionElement always has its one
// child, built by ContainerDescription::CreateInstance.
void ClipRegionElement::ComputeSize() {
    if ((_flags & kElementFlagNeedsSizeCalc) == 0) return;

    _layoutWidth  = _reqWidth;
    _layoutHeight = _reqHeight;

    _children._data[0]->GetPaddedSize(_innerWidth, _innerHeight);   // 0x180115f10

    _flags &= ~UI::kElementFlagNeedsSizeCalc;
}

// ============================================================================
// ?ComputeLayout@ClipRegionElement@UI@@UEAAXHHHH@Z   0x1800cba00
// ============================================================================
// The standard aligned rect for the viewport, then the child laid out at its
// FULL padded size, shifted by the scroll offset. The child overhangs the
// viewport by design — Draw's scissor is what hides the overhang.
//
// The child pointer is loaded TWICE: once for the visibility test and the size
// measurement, and again for the ComputeLayout call (0x1800cc7a8 re-reads
// `_children[0]`). Both are transcribed.
void ClipRegionElement::ComputeLayout(int32_t x, int32_t y, int32_t w, int32_t h) {
    ElementComputeAlignedRect(this, x, y, w, h);

    Element* child = _children._data[0];
    if (child == nullptr) return;
    if ((child->_flags & UI::kElementFlagHidden) != 0) return;

    int cw = 0, ch = 0;
    PaddedSizeInline(child, &cw, &ch);

    Element* again = _children._data[0];
    const int cx = _layoutLeft - _offsetX;
    const int cy = _layoutTop  - _offsetY;
    again->ComputeLayout(cx, cy, cx + cw, cy + ch);
}

// ============================================================================
// ?Draw@ClipRegionElement@UI@@UEBAXAEAVSpriteContext@2@E@Z   0x1800cb900
// ============================================================================
// Push the viewport as the sprite scissor, draw every visible child, restore.
//
// *** THE SCISSOR IS A {x, y, WIDTH, HEIGHT} RECTANGLE, NOT {left, top, right,
// bottom}, AND THE EXTENTS ARE INCLUSIVE. *** Retail builds it as
// `{left, top, right - left + 1, bottom - top + 1}` (0x1800cb943..0x1800cb96a).
// Dropping the `+ 1` loses the last row and column of every scrolled panel.
//
// The two SetScissor calls SWAP their two buffers: the first writes the old
// viewport into scratch and takes the new one from `rect`; the second writes
// into `rect` (now dead) and takes the saved one back. Reproduced with the same
// two buffers rather than one, because SetScissor writes through its out slot.
void ClipRegionElement::Draw(SpriteContext& context, uint8_t alpha) const {
    const Element* border = _border;
    if (border != nullptr) border->Draw(context, alpha);

    alpha = DimAlpha(this, alpha);

    Viewport rect;
    rect.x = _layoutLeft;
    rect.y = _layoutTop;
    rect.width  = (_layoutRight  - rect.x) + 1;
    rect.height = (_layoutBottom - rect.y) + 1;

    // ?SetScissor@SpriteContext@UI@@QEAA?AVViewport@Video@@AEBV34@@Z (0x18011d050):
    // RETSLOT, the previous scissor returned into `saved`.
    Viewport saved = context.SetScissor(rect);

    const int32_t count = _children._size;
    for (int32_t i = 0; i < count; ++i) {
        const Element* child = _children._data[i];
        if (child == nullptr) continue;
        if ((child->_flags & UI::kElementFlagHidden) != 0) continue;
        child->Draw(context, alpha);
    }

    rect = context.SetScissor(saved);         // the retslot is `rect` again
}

// ============================================================================
// ?Update@ClipRegionElement@UI@@   0x1800cbd70
// ?Contains@ClipRegionElement@UI@@UEBA_NHHPEAVUserInterface@2@@Z  0x1800cbcc0
// ============================================================================
// The same idiom twice: install THIS element's layout rect as the
// UserInterface's clip rectangle, run the base body (QUALIFIED — whose hit
// testing consults it), then put the caller's rectangle back. Without this,
// an element scrolled out of a panel would still take the mouse.
//
// The save reads the four dwords BEFORE any of them is overwritten; a
// field-by-field save/overwrite would corrupt the second and later reads.
void ClipRegionElement::Update(const System::Time& time, UserInterface* ui,
                               Object::InternalPtr<DialogInstance>* dialog,
                               const UpdateState& state) {

    const int32_t l = _layoutLeft;
    const int32_t t = _layoutTop;
    const int32_t r = _layoutRight;
    const int32_t b = _layoutBottom;

    const int32_t sl  = ui->_clipRegion.left;
    const int32_t st_ = ui->_clipRegion.top;
    const int32_t sr  = ui->_clipRegion.right;
    const int32_t sb  = ui->_clipRegion.bottom;

    ui->_clipRegion.left   = l;
    ui->_clipRegion.top    = t;
    ui->_clipRegion.right  = r;
    ui->_clipRegion.bottom = b;

    Object::InternalPtr<DialogInstance> dlgCopy = *dialog;
    Element::Update(time, ui, &dlgCopy, state);

    ui->_clipRegion.left   = sl;
    ui->_clipRegion.top    = st_;
    ui->_clipRegion.right  = sr;
    ui->_clipRegion.bottom = sb;
}

bool ClipRegionElement::Contains(int32_t x, int32_t y, UserInterface* ui) const {

    const int32_t l = _layoutLeft;
    const int32_t t = _layoutTop;
    const int32_t r = _layoutRight;
    const int32_t b = _layoutBottom;

    const int32_t sl  = ui->_clipRegion.left;
    const int32_t st_ = ui->_clipRegion.top;
    const int32_t sr  = ui->_clipRegion.right;
    const int32_t sb  = ui->_clipRegion.bottom;

    ui->_clipRegion.left   = l;
    ui->_clipRegion.top    = t;
    ui->_clipRegion.right  = r;
    ui->_clipRegion.bottom = b;

    const bool inside = Element::Contains(x, y, ui);

    ui->_clipRegion.left   = sl;
    ui->_clipRegion.top    = st_;
    ui->_clipRegion.right  = sr;
    ui->_clipRegion.bottom = sb;
    return inside;
}

}  // namespace UI

// ============================================================================
// ??1ClipRegionElement@UI@@UEAA@XZ   0x1800cbe30
// ============================================================================
// Restamp, then `JMP ~Element`: nothing of its own. ENDGAME B.3.9 UI-A: the
// body is EMPTY and the compiler restamps and chains ~Element.
namespace UI {
ClipRegionElement::~ClipRegionElement() {}

// ?CreateNewObject@ClipRegionElement@UI@@SAPEAVBase@Object@@XZ  0x180038060
// The FACTUI1 row this replaces: MemAlloc(0xd8, 8), retail's null branch,
// then the inlined constructor (the same stores as ??0 0x1800cbe60).
Object::Base* ClipRegionElement::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ClipRegionElement), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) ClipRegionElement();
}
// ?CreateInPlaceObject@ClipRegionElement@UI@@SAPEAVBase@Object@@PEAX@Z  0x180038110
Object::Base* ClipRegionElement::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) ClipRegionElement();
}
}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::ClipRegionElement's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@ClipRegionElement@UI@@ (0x1801f9d68): name "ClipRegionElement", parent
// "Element" (retail ?_ParentTypeName@ClipRegionElement@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::ClipRegionElement, "Element", nullptr)
