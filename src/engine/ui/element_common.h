#pragma once
// ============================================================================
// src/engine/ui/element_common.h — the four sequences every UI::*Element body
// inlines, transcribed ONCE
// ============================================================================
//
// 2026-09-12, vtable batch 3 (the first half of the `UI::*Element` widget
// runtime: AnimatedImage, Border, Button, Check, ClipRegion, Combo, Container,
// DocumentText, Edit, Graph, Group, Image, Label).
//
// Retail compiles `UI::Element`'s small helpers INTO every widget body, so the
// same four sequences appear verbatim in a dozen `ComputeSize` / `ComputeLayout`
// / `Draw` implementations. They are written once here rather than a dozen
// times, because a transcription error in one copy would be invisible against
// the other eleven. Each helper cites the instruction range of ONE of the
// copies it was read from; every other copy was diffed against it.
//
// WHAT IS *NOT* HERE, deliberately:
//   * anything that differs between widgets. `GroupElement::ComputeLayout`
//     (0x180118050) uses a DIFFERENT alignment sequence — it stretches
//     unconditionally and never consults flag bits 2/3 — so it is written out
//     in groupelement.cpp instead of being bent into `ElementComputeAlignedRect`.
//   * `ContainerElement`'s two ONE-AXIS copies of the padded-size sequence:
//     they read only the width (or only the height) and the second half of
//     `PaddedSizeInline` would be a read retail does not perform. They are
//     written out in containerelement.cpp.
//
// NAME NOTE: a SECOND `src/engine/ui/widget_common.h` exists, written
// independently by the elem_B batch for the other fourteen widget classes.
// The two headers overlap heavily but not exactly, so this one keeps its own
// file name and its own namespace (`rtuielem`, not `rtui`) rather than being
// merged by hand into a single header that neither batch has re-verified.
// Folding them is a readability job for after cutover, not a merge-time one.
//
// PORTABILITY: no <windows.h> (PLAN.md §4.1 decision 1).
// ============================================================================

#include <cstddef>
#include <cstdint>

// ENDGAME B.3.6 block A: the real `UI::Element` class, so the two inline
// helpers below dispatch slots 4/5 as member calls instead of raw slot reads.
#include "ui/Element.h"
#include "video/Device.h"   // ENDGAME B.3.9 SS1: Video::Device::_singleton
#include "ui/DialogInstance.h"   // B4: Element::_dialogParent->_ui
#include "ui/UserInterface.h"    // B4: UserInterface::_uiScale
#include "ui/Sprite.h"           // B4: SpriteW / SpriteH
#include "ui/SpriteSheet.h"      // B4: SpriteSheet::m_sprites
#include "system/MemoryAllocator.h"   // B3R RT-UI: MemAlloc / MemFree, direct

namespace rtuielem {

// (B3X W0: `VTableOf`, `VSlot<Fn>(o, INDEX)`, the `kSlot*` Element slot
// indices and the four slot typedefs are gone -- every UI::Element slot is
// dispatched by name, `static_cast<UI::Element*>(e)->ComputeSize()`.)
// (B4: the `kElem*` offsets and the I32 / U32 / P / Pc / Child byte accessors
// are gone -- every UI::Element field is read by name.)

// (The Element::_flags bits are Element.h's UI::kElementFlag* names.)

// ---------------------------------------------------------------------------
// `?SetNeedsSizeCalc@Element@UI@@` (0x180035470), inlined everywhere.
// `OR dword [RCX+0x34],0x8000000 ; RCX = [RCX+0x98]` until the chain ends.
// It is a DO-WHILE: `this` is always marked, even when `this` has no parent.
// ---------------------------------------------------------------------------
inline void SetNeedsSizeCalc(UI::Element* self) {
    UI::Element* n = self;
    do {
        n->_flags |= UI::kElementFlagNeedsSizeCalc;
        n = n->_parent;
    } while (n != nullptr);
}

// ---------------------------------------------------------------------------
// `?SetFlagsOnOff@Element@UI@@` inlined: write the new flag word, and run the
// dirty walk ONLY IF BIT 0 CHANGED (`XOR old,new ; TEST AL,1`). Every widget
// Update that touches +0x34 uses this exact shape; getting the test wrong
// (walking unconditionally) is invisible in a screenshot and costs a parent
// walk per element per frame.
// ---------------------------------------------------------------------------
inline void StoreFlags(UI::Element* self, uint32_t newFlags) {
    const uint32_t old = self->_flags;
    self->_flags = newFlags;
    if (((old ^ newFlags) & UI::kElementFlagHidden) != 0) SetNeedsSizeCalc(self);
}

// ---------------------------------------------------------------------------
// UI::Sprite (src/engine/ui/Sprite.h): {SpriteSheet* +0x00, int index
// +0x08, int pad +0x0c}. A sprite's pixel size is read out of the sheet's frame
// table: `SpriteSheet::m_sprites[index]`, width at +0x1c and height at +0x20.
//
// Read from `?ComputeSize@ImageElement@UI@@` 0x180118650:
//     iVar2 = *(int*)(idx*0x38 + 0x1c + *(longlong*)(*(longlong*)(this+0xc0)+0x20))
// A NEGATIVE index yields 0 AND DOES NOT TOUCH THE SHEET POINTER — retail
// tests the index first, so a sprite with index -1 and a null sheet is legal.
// LEFT: UI::Sprite::spriteSheet is `void*` (Sprite.h, block R-UI-b).
// ---------------------------------------------------------------------------
inline int SpriteW(const UI::Sprite& sprite) {
    const int32_t idx = sprite.spriteIndex;
    if (idx < 0) return 0;
    const UI::SpriteSheet* sheet = sprite.spriteSheet;
    return sheet->m_sprites[idx].width;
}
inline int SpriteH(const UI::Sprite& sprite) {
    const int32_t idx = sprite.spriteIndex;
    if (idx < 0) return 0;
    const UI::SpriteSheet* sheet = sprite.spriteSheet;
    return sheet->m_sprites[idx].height;
}

// ---------------------------------------------------------------------------
// `?ComputeColor@Element@UI@@` (0x180035490), inlined by every Draw.
// Read from 0x180118886..0x1801188d6 (ImageElement::Draw):
//
//    ECX = (color >> 8) & 0xffff0000 ; ECX *= alpha ; ECX ^= color
//    ECX &= 0xffffff
//    R8D = (color >> 8) & 0xff0000   ; R8D *= alpha ; ECX ^= R8D
//
// The two masks 0xffff0000 and 0xff0000 select the SAME bits once `color >> 8`
// has already cleared the top byte, so both products are `alphaByte * alpha`
// shifted into bits 16.., and the result is
// `(color & 0xffffff) | (((alphaByte*alpha) >> 8) << 24)`. The shipped bit
// juggling is transcribed rather than the simplification, because the
// simplification is only equal while the top byte of `color >> 8` is zero and
// nothing in this file enforces that.
// ---------------------------------------------------------------------------
inline uint32_t ComputeColor(uint32_t color, uint8_t alpha) {
    uint32_t c = ((color >> 8) & 0xffff0000u) * static_cast<uint32_t>(alpha);
    c ^= color;
    c &= 0x00ffffffu;
    const uint32_t hi = ((color >> 8) & 0x00ff0000u) * static_cast<uint32_t>(alpha);
    return c ^ hi;
}

// The `TEST [this+0x34],0x40000 / SHR alpha,1` prologue every Draw runs.
inline uint8_t DimAlpha(const UI::Element* self, uint8_t alpha) {
    return (self->_flags & UI::kElementFlagDimColor) ? static_cast<uint8_t>(alpha >> 1) : alpha;
}

// ---------------------------------------------------------------------------
// `?GetPaddedSize@Element@UI@@` (0x180115f10), inlined.
// Transcribed from `?ComputeSize@ButtonElement@UI@@` 0x180110d5a..0x180110e0a;
// identical to the already-ported out-of-line body in src/engine/ui/element.cpp.
// Element::_dialogParent -> DialogInstance::_ui -> UserInterface::_uiScale;
// Video::Device's back-buffer width / height.
// ---------------------------------------------------------------------------
inline void PaddedSizeInline(UI::Element* e, int* outW, int* outH) {
    e->ComputeSize();                                   // slot 4, virtual

    *outW = e->_layoutWidth  + e->_rightPad  + e->_leftPad;
    *outH = e->_layoutHeight + e->_bottomPad + e->_topPad;
    if (*outW < e->_minWidth)  *outW = e->_minWidth;
    if (*outH < e->_minHeight) *outH = e->_minHeight;

    const uint32_t f = e->_flags;
    if ((f & (UI::kElementFlagSizeToScreenH | UI::kElementFlagSizeToScreenW)) != 0) {
        UI::Element* root = e;
        while (root->_parent != nullptr) root = root->_parent;
        const ::Video::Device* dev = ::Video::Device::_singleton;
        if (f & UI::kElementFlagSizeToScreenW) {
            const UI::UserInterface* owner = root->_dialogParent->_ui;
            *outW = static_cast<int>(static_cast<float>(dev->backBufferWidth) * owner->_uiScale);
        }
        if (f & UI::kElementFlagSizeToScreenH) {
            const UI::UserInterface* owner = root->_dialogParent->_ui;
            *outH = static_cast<int>(static_cast<float>(dev->backBufferHeight) * owner->_uiScale);
        }
    }
}

// ---------------------------------------------------------------------------
// The standard `ComputeLayout` body: the border forward, `ComputeSize` through
// virtual slot 4, and the inlined member `?GetAlignedPosition@Element@UI@@`
// (0x180115ff0 -> the static 0x1801158e0), writing the four layout fields.
//
// Transcribed from `?ComputeLayout@ImageElement@UI@@` 0x1801186f0 and diffed
// instruction for instruction against ButtonElement (0x180110e70),
// ClipRegionElement (0x1800cba00), ContainerElement (0x1800cc620),
// DocumentTextElement (0x1800cda00), EditElement (0x1800d0ab0),
// GraphElement (0x1800d16b0) and LabelElement (0x1800d2330). The eight differ
// only in the ORDER of the four stores at the end, which is register-allocation
// noise: the values are identical and the fields do not alias.
//
// TWO FAITHFULNESS TRAPS, the same two src/engine/ui/element.cpp names for the
// out-of-line copy:
//   * the centring halves are ARITHMETIC shifts (`SAR`), not `/ 2`;
//   * `sw`/`sh` are read, possibly REPLACED by the stretch, then read again by
//     the placement — the order is load-bearing.
//
// The border call forwards the ORIGINAL x/y/w/h (EDX/R8D/R9D are untouched at
// 0x18011872a and h is re-spilled to the outgoing slot), not the clamped ones.
// ---------------------------------------------------------------------------
inline void ElementComputeAlignedRect(UI::Element* self, int x, int y, int w, int h) {
    UI::Element* border = self->_border;
    if (border != nullptr)
        border->ComputeLayout(x, y, w, h);   // slot 5

    self->ComputeSize();                                // slot 4

    int sw = self->_layoutWidth;
    int sh = self->_layoutHeight;
    const int align = self->_alignment;

    if (w < sw + x) x = w - sw;
    if (h < sh + y) y = h - sh;

    int cx = x + self->_leftPad;
    int cy = y + self->_topPad;
    const int availW = (w - self->_rightPad)  - cx;
    const int availH = (h - self->_bottomPad) - cy;

    const uint32_t flags = self->_flags;
    if ((flags & UI::kElementFlagAlignArg1) != 0) sw = availW;
    if ((flags & UI::kElementFlagAlignArg2) != 0) sh = availH;

    switch (align) {
    case UI::ElementDescription::TopCenter:
    case UI::ElementDescription::MidCenter:
    case UI::ElementDescription::BottomCenter:
        cx = ((availW >> 1) - (sw >> 1)) + cx;
        break;
    case UI::ElementDescription::TopRight:
    case UI::ElementDescription::MidRight:
    case UI::ElementDescription::BottomRight:
        cx = (w - self->_rightPad) - sw;
        break;
    default: break;
    }
    if (align > UI::ElementDescription::TopRight) {
        if (align < UI::ElementDescription::BottomLeft)
            cy = ((availH >> 1) - (sh >> 1)) + cy;
        else if (align <= UI::ElementDescription::BottomRight)
            cy = (h - self->_bottomPad) - sh;
    }

    self->_layoutLeft   = cx;
    self->_layoutTop    = cy;
    self->_layoutRight  = cx + sw;
    self->_layoutBottom = cy + sh;
}

// (B4: TypeIdOf / TypeNameOf had no caller left; deleted.)

}  // namespace rtuielem


// (ENDGAME B.3.9 TI-UI: RTELEM_TYPE_SLOTS / RTELEM_TYPE_BODIES, the by-name
// `?_typeInfo@<NAME>@UI@@` / `??_7<NAME>@UI@@` accessors and the Type/TypeName
// slot bodies over them, had no expansion left; deleted -- every class carries
// its own `DECLARE_TYPE` record.)
