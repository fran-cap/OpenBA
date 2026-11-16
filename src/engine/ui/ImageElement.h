#pragma once
// ============================================================================
// src/engine/ui/ImageElement.h — UI::ImageElement, the REAL C++ class
// ============================================================================
//
// Runtime-x64.dll class, 0xE0 bytes. Until ENDGAME B.3 step 6 block C2 this
// header held a BARE `struct ImageElement` (an opaque 0xB8-byte `elementBase`
// blob + the fields); RULE B6-4.2 moves it under retail's name as the real
// class deriving from its PROVED parent. Nothing embedded the bare struct, so
// no POD survives; its one field reader (element_sprite_debug.cpp) now spells
// the class's members.
//
// PARENT `UI::Element` — READ, not inferred (B36_SLOTS.md section 3):
//     ?_ParentTypeName@ImageElement@UI@@CAPEBDXZ  @0x180036280 -> "Element"
// RULE B5-15 / B6-7 — the two literals, which DEFINE_TYPE (imageelement.cpp) carries:
//     ?_TypeName@ImageElement@UI@@CAPEBDXZ        @0x180036270 -> "ImageElement"
//     ?_ParentTypeName@ImageElement@UI@@CAPEBDXZ  @0x180036280 -> "Element"
//
// `??_7ImageElement@UI@@6B@` @ 0x1801560b0, TWELVE slots:
//   [ 0] 0x180036600  (unexported) vector deleting dtor — the class's own (B.3.9)
//   [ 1] 0x1800362b0  TypeName
//   [ 2] 0x180036290  Type
//   [ 3] 0x180116870  Element::Persist        INHERITED
//   [ 4] 0x180118650  ComputeSize             (also AnimatedImageElement's)
//   [ 5] 0x1801186f0  ComputeLayout           (also AnimatedImageElement's)
//   [ 6] 0x180118850  Draw                    (also AnimatedImageElement's)
//   [ 7..11]          Element's               INHERITED
// ONE SUBCLASS (RULE B6-9): `AnimatedImageElement` ("ImageElement"), which
// overrides only Update.
//
// ---------------------------------------------------------------------------
// LAYOUT — 0xE0 bytes (`MOV EDX,0xe0` in the deleting dtor 0x180036600)
// ---------------------------------------------------------------------------
// `??0ImageElement@UI@@QEAA@XZ` (0x180118410; the same stores are inlined at
// 0x1800cb418..0x1800cb42f in ??0AnimatedImageElement, read this pass):
//     ??0Element ; stamp ; MOV qword [+0xc0],0 ; MOV dword [+0xc8],-1 ;
//     MOV dword [+0xd0],0 ; MOV qword [+0xd4],-1   (UNALIGNED: +0xd4 AND +0xd8)
// so +0xcc (the Sprite's pad) and +0xdc are written by no ctor. The copy ctor
// (0x1800364c0) copies +0xc0/+0xc8 as two qwords and +0xd0/+0xd4/+0xd8 as
// three dwords.
//   +0xC0  _sprite    UI::Sprite by value (RULE B6-10). `?GetSprite@` 0x1801182a0
//                     / `?SetSprite@` 0x180118600 move all 16 bytes.
//   +0xD0  _rotation  float. `?SetRotation@` 0x1801182c0 (a plain store, NO
//                     dirty walk); Draw passes it to the rotated AddSprite.
//   +0xD4  _width     `?SetWidthHeight@` 0x180118260 stores arg 1 here; ctor -1;
//                     ComputeSize uses it when > 0, else the sprite width.
//   +0xD8  _height    `?SetWidthHeight@` arg 2; ctor -1; likewise for height.
//   +0xDC  _padDC     written by nothing.
//
// (Until ENDGAME B.3.9:) ABSTRACT (slot 0 `= 0`); no real ctor — the factory door (ui_factory_
// wave28.cpp) and ImageDescription::CreateInstance's inlined copy build every
// live one, wearing the MINTED table.
//
// ENDGAME B.3 step 9 (block UI-A, B39_RULES D1/D2): CONCRETE. The real public
// ctor is retail's `??0ImageElement@UI@@QEAA@XZ` (0x180118410) minus its vptr stores;
// slot 0 is the class's own vector deleting dtor; ~ImageElement is public and
// virtual. The factory pair is the class's own; the minted
// `??_7ImageElement@UI@@6B@`, its RT_DELDTOR row, the factory-interpreter row and
// the doors are retired.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>

#include "Element.h"
// UI::Sprite — ONE definition, src/engine/ui/Sprite.h (RULE B6-4.2).
#include "Sprite.h"
#include "../object/Internal.h"   // Object::detail::InternalMemFree

namespace UI {

class ImageElement : public Element {
public:
    // ??0ImageElement@UI@@QEAA@XZ 0x180118410 — the REAL constructor: CALL
    // ??0Element (Element()), then `MOV qword [+0xc0],0 ; MOV dword [+0xc8],-1`
    // (the Sprite's own default ctor, {nullptr, -1}; its pad +0xcc NOT
    // written), `MOV dword [+0xd0],0`, and ONE unaligned QWORD -1 at +0xd4
    // covering +0xd4 and +0xd8. +0xdc is NOT written.
    ImageElement() : Element() {
        _rotation = 0.0f;
        _width  = -1;
        _height = -1;
    }

    // ---- the twelve slots, in measured order ------------------------------
    // [0] 0x180036600 — MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0xe0`, the element size).
    // `??1ImageElement@UI@@UEAA@XZ` (0x180118470). EMPTY
    // (imageelement.cpp): restamp, then ~Element, which the compiler chains.
    ~ImageElement() override;
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x1800362b0, Type 0x180036290, TypeId 0x1800362a0, _TypeName 0x180036270
    //         _ParentTypeName 0x180036280, CreateNewObject 0x1800363a0, CreateInPlaceObject 0x180036450
    DECLARE_TYPE(ImageElement)
public:
    // [3] Persist — Element's, inherited.
    void ComputeSize() override;                                  // [4] 0x180118650
    void ComputeLayout(int32_t x, int32_t y,
                       int32_t w, int32_t h) override;            // [5] 0x1801186f0
    void Draw(SpriteContext& context, uint8_t alpha) const override; // [6] 0x180118850
    // [7..11] Element's, inherited.

    // ---- non-virtual — PORTED; the four bodies are DEFINED in
    // widgets_layout.cpp (fragment pass).
    // `?GetSprite@ImageElement@UI@@QEBA?AVSprite@2@XZ` is a hidden-retslot
    // export; the member returns by value.
    Sprite GetSprite() const;                                     // 0x1801182a0  RETSLOT
    void   SetRotation(float degrees);                            // 0x1801182c0
    // `?SetSprite@ImageElement@UI@@QEAAXVSprite@2@@Z` — the 0x10-byte Sprite BY
    // VALUE, which retail passes by HIDDEN POINTER (RULE B6-11): the register
    // holds the ADDRESS of the caller's copy, so the member takes the pointer
    // (fragment pass F1; was declared by value).
    void   SetSprite(const Sprite* sprite);                       // 0x180118600
    void   SetWidthHeight(int32_t width, int32_t height);         // 0x180118260

    // ---- exported, NOT PORTED as members ----------------------------------
    ImageElement(const ImageElement& src);                        // 0x1800364c0  not ported (door retired: no caller)
    ImageElement& operator=(const ImageElement& src);             // 0x180036540  not ported

private:

public:
    Sprite   _sprite;         // +0xC0 .. +0xCF
    float    _rotation;       // +0xD0  ctor 0
    int32_t  _width;          // +0xD4  ctor -1 (one qword store with +0xD8)
    int32_t  _height;         // +0xD8  ctor -1
    uint32_t _padDC;          // +0xDC  written by nothing
};

static_assert(sizeof(ImageElement) == 0xE0,
              "UI::ImageElement is 0xE0 bytes (MOV EDX,0xe0 in 0x180036600)");
static_assert(offsetof(ImageElement, _padDC) == 0xDC, "ImageElement::_padDC");
static_assert(alignof(ImageElement) == 8, "UI::ImageElement aligns to 8");
static_assert(std::has_virtual_destructor<ImageElement>::value, "B3X W1: the normal-virtual cutover");
static_assert(!std::is_abstract<ImageElement>::value, "B39 D1: concrete");
static_assert(offsetof(ImageElement, _sprite) == sizeof(Element),
              "ImageElement's own fields begin at the end of UI::Element");
static_assert(offsetof(ImageElement, _sprite)   == 0xC0, "sprite at +0xC0");
static_assert(offsetof(ImageElement, _sprite) + offsetof(Sprite, spriteIndex) == 0xC8,
              "sprite index at +0xC8");
static_assert(offsetof(ImageElement, _rotation) == 0xD0, "rotation at +0xD0");
static_assert(offsetof(ImageElement, _width)    == 0xD4, "width override at +0xD4");
static_assert(offsetof(ImageElement, _height)   == 0xD8, "height override at +0xD8");

}  // namespace UI
