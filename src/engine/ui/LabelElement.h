#pragma once
// ============================================================================
// src/engine/ui/LabelElement.h — UI::LabelElement (0x130 bytes)
// ============================================================================
//
// Inheritance: UI::Element (0xC0) -> UI::TextElement (0x100) -> UI::LabelElement
//
//   Default ctor:  0x1800d2620 (??0LabelElement@UI@@QEAA@XZ)
//   Copy ctor:     0x180042a00 (??0LabelElement@UI@@QEAA@AEBV01@@Z)
//   Dtor:          0x1800d25f0 (??1LabelElement@UI@@UEAA@XZ)
//   SetSprite:     0x180042980 (?SetSprite@LabelElement@UI@@QEAAXVSprite@2@@Z)
//   GetSprite:     0x1800429d0 (?GetSprite@LabelElement@UI@@QEBA?AVSprite@2@XZ)
//
// ENDGAME B.3 step 6, block C3 — THE DEFECT THIS HEADER CARRIED, AND ITS FIX.
// Until this block the file held a BARE `struct LabelElement` whose TextElement
// part (+0xC0..+0xFF) used an old table this header itself marked WRONG
// (`textStringC0/textDataC8/fontPtrD0/fieldD8/textColorE0`), pinned by twenty
// `static_assert`s at the right OFFSETS under the wrong NAMES. TextElement is a
// real class now (TextElement.h, its layout read off its ctor 0x18011eb80), so:
//   * `UI::LabelElement` below derives from `UI::TextElement` and takes that
//     class's names for the inherited prefix; it pins only its OWN +0x100..+0x12f;
//   * the old bare POD moved to `RtUI::LabelElement` (B4b-1) with its TextElement
//     names CORRECTED to TextElement's and every one of its offset asserts kept.
//     Its one remaining user is src/engine/ui/element_sprite_debug.cpp, which
//     reads the sprite triple for a debug print (the calls there are members).
//
// LabelElement's OWN LAYOUT, every field a store of `??0LabelElement@UI@@QEAA@XZ`
// (0x1800d2620, read as DISASSEMBLY by block C3; `CALL ??0TextElement` first):
//     +0x100 qword 0     _sprite.spriteSheet   UI::Sprite, the icon
//     +0x108 dword -1    _sprite.spriteIndex   (+0x10c NOT written)
//     +0x110 dword 3     _align        the icon/text pair's inner alignment 0..8
//     +0x114 QWORD 1     _mode         0 icon right, 1 left, 2 below, 3 above
//                        _minTextWidth (the high half of the same store: 0)
//     +0x11c dword 2     _gap          pixels between icon and text
//     +0x120 QWORD -1    _iconWidth    override, <1 = use the sprite
//                        _iconHeight   override, <1 = use the sprite
//     +0x128 dword -1    _iconColor    SetColor / SetSpriteColor write it
//     +0x12c dword 0     _textWidth    an OUTPUT of ComputeSize
// Names: labelelement.cpp's port, which read each one's use in ComputeSize /
// Draw; `_iconColor` is also `?SetSpriteColor@` (0x1800429f0:
// `MOV [RCX+0x128],EDX / RET`).
// SIZE 0x130: `MOV EDX,0x130` in the slot-0 vector deleting dtor 0x180042bb0.
//
// ENDGAME B.3 step 9 (block UI-A, B39_RULES D1/D2): CONCRETE. The real public
// ctor is retail's `??0LabelElement@UI@@QEAA@XZ` (0x1800d2620) minus its vptr stores;
// slot 0 is the class's own vector deleting dtor; ~LabelElement is public and
// virtual. The factory pair is the class's own; the minted
// `??_7LabelElement@UI@@6B@`, its RT_DELDTOR row, the factory-interpreter row and
// the doors are retired.
// ============================================================================

#include <type_traits>
#include <cstdint>
#include <cstddef>

#include "Element.h"
#include "TextElement.h"
#include "Sprite.h"
#include "../object/Internal.h"   // Object::detail::InternalMemFree

// ############################################################################
// RtUI::LabelElement — the old bare POD, moved (B4b-1), names corrected.
// ############################################################################




// ############################################################################
// ENDGAME B.3 step 6 (block C3) — `UI::LabelElement`, the REAL C++ class
// ############################################################################
//
// PARENT `UI::TextElement` — READ (B36_SLOTS.md section 3), RECORDED, NOT USED:
//     ?_TypeName@LabelElement@UI@@CAPEBDXZ       @0x1800427b0 -> "LabelElement"
//     ?_ParentTypeName@LabelElement@UI@@CAPEBDXZ @0x1800427c0 -> "TextElement"
//
// `??_7LabelElement@UI@@6B@` @ 0x180156ad0, TWELVE slots: [0] 0x180042bb0
// (deleting_dtor.cpp row) · [1] 0x1800427f0 TypeName · [2] 0x1800427d0 Type ·
// [4] 0x1800d2490 ComputeSize · [5] 0x1800d2330 ComputeLayout · [6] 0x1800d1c80
// Draw · [10] 0x180042970 SetColor — the rest inherited through TextElement.
//
// (Until ENDGAME B.3.9:) ABSTRACT in C++; no real ctor: live ones are built by the `??0LabelElement`
// door (ui_factory_wave28.cpp) and wear the MINTED table.
// ############################################################################
namespace UI {

class LabelElement : public TextElement {
public:
    // ??0LabelElement@UI@@QEAA@XZ 0x1800d2620 — the REAL constructor: CALL
    // ??0TextElement (TextElement()), then the icon Sprite's own default ctor
    // ({nullptr, -1}; +0x10c NOT written), +0x110 DWORD 3, ONE QWORD 1 over
    // +0x114/+0x118, +0x11c DWORD 2, ONE QWORD -1 over +0x120/+0x124, +0x128
    // DWORD -1, +0x12c DWORD 0.
    LabelElement() : TextElement() {
        _align        = ElementDescription::MidLeft;
        _mode         = 1;
        _minTextWidth = 0;
        _gap          = 2;
        _iconWidth    = -1;
        _iconHeight   = -1;
        _iconColor    = System::kColorWhite;
        _textWidth    = 0;
    }

    // [0] 0x180042bb0 — MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0x130`, the element size).
    // `??1LabelElement@UI@@UEAA@XZ` (0x1800d25f0). EMPTY
    // (labelelement.cpp): restamp, then the tail JMP to ~TextElement, which the
    // compiler chains.
    ~LabelElement() override;
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x1800427f0, Type 0x1800427d0, TypeId 0x1800427e0, _TypeName 0x1800427b0
    //         _ParentTypeName 0x1800427c0, CreateNewObject 0x1800428e0, CreateInPlaceObject 0x180042960
    DECLARE_TYPE(LabelElement)
public:
    void ComputeSize() override;                                 // [4] 0x1800d2490
    void ComputeLayout(int32_t x, int32_t y,
                       int32_t w, int32_t h) override;           // [5] 0x1800d2330
    void Draw(SpriteContext& context, uint8_t alpha) const override; // [6] 0x1800d1c80
    void SetColor(uint32_t color) override;                      // [10] 0x180042970

    // ?GetSprite@LabelElement@UI@@QEBA?AVSprite@2@XZ  0x1800429d0 — RETSLOT in
    // retail; by value here. DEFINED in widgets_layout.cpp (fragment pass),
    // whose registered door placement-news the result.
    Sprite GetSprite() const;
    // ?SetSprite@LabelElement@UI@@QEAAXVSprite@2@@Z  0x180042980. `VSprite@2@`
    // is the 0x10-byte UI::Sprite BY VALUE, which MSVC passes by HIDDEN POINTER
    // (the RULE B6-11 shape): declared with the pointer, in retail order. Its
    // body is DEFINED in widgets_layout.cpp (fragment pass).
    void SetSprite(const Sprite* sprite);
    // ?SetSpriteColor@LabelElement@UI@@QEAAXI@Z  0x1800429f0 —
    // `MOV [RCX+0x128],EDX / RET`, the whole body.
    void SetSpriteColor(uint32_t color) { _iconColor = color; }

    LabelElement(const LabelElement& src);                       // 0x180042a00  not ported (door retired: no caller)
    LabelElement& operator=(const LabelElement& src);            // 0x180042ac0  not ported
    static void* operator new(std::size_t bytes);                // 0x180042800  not ported
    static void* operator new(std::size_t bytes, void* place);   // 0x180042870  not ported
    static void  operator delete(void* p) { ::Object::detail::InternalMemFree(p); }                       // 0x180042880  not ported

private:

public:
    // ---- the 0x30 bytes LabelElement adds -----------------------------------
    Sprite   _sprite;         // +0x100  the icon
    int32_t  _align;          // +0x110  ctor 3
    uint32_t _mode;           // +0x114  ctor 1
    int32_t  _minTextWidth;   // +0x118  ctor 0
    int32_t  _gap;            // +0x11c  ctor 2
    int32_t  _iconWidth;      // +0x120  ctor -1
    int32_t  _iconHeight;     // +0x124  ctor -1
    uint32_t _iconColor;      // +0x128  ctor -1
    int32_t  _textWidth;      // +0x12c  ctor 0
};

static_assert(sizeof(LabelElement) == 0x130,
              "UI::LabelElement is 0x130 (MOV EDX,0x130 in 0x180042bb0)");
static_assert(alignof(LabelElement) == 8, "UI::LabelElement aligns to 8");
static_assert(std::has_virtual_destructor<LabelElement>::value, "B3X W1: the normal-virtual cutover");
static_assert(!std::is_abstract<LabelElement>::value, "B39 D1: concrete");
static_assert(offsetof(LabelElement, _sprite) == sizeof(TextElement),
              "LabelElement's own fields begin at the end of TextElement");
static_assert(offsetof(LabelElement, _sprite)       == 0x100, "");
static_assert(offsetof(LabelElement, _align)        == 0x110, "");
static_assert(offsetof(LabelElement, _mode)         == 0x114, "");
static_assert(offsetof(LabelElement, _minTextWidth) == 0x118, "");
static_assert(offsetof(LabelElement, _gap)          == 0x11c, "");
static_assert(offsetof(LabelElement, _iconWidth)    == 0x120, "");
static_assert(offsetof(LabelElement, _iconHeight)   == 0x124, "");
static_assert(offsetof(LabelElement, _iconColor)    == 0x128, "");
static_assert(offsetof(LabelElement, _textWidth)    == 0x12c, "");
// The class and the RtUI POD describe the same bytes.

}  // namespace UI
