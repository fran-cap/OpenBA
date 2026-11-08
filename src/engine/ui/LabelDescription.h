#pragma once
// ============================================================================
// src/engine/ui/LabelDescription.h — UI::LabelDescription, the REAL C++ class
// ============================================================================
//
// ENDGAME B.3 step 6, block C3. The POD `RtUI::LabelDescription` stays in
// DescriptionsBatch1.h untouched (RULE B6-4.4 — the closing pass turns it into
// an alias); this class is cross-asserted against it at the bottom.
//
// PARENT `UI::TextDescription` — READ (B36_SLOTS.md section 3), RECORDED, NOT USED:
//     ?_TypeName@LabelDescription@UI@@CAPEBDXZ       @0x180042370 -> "LabelDescription"
//     ?_ParentTypeName@LabelDescription@UI@@CAPEBDXZ @0x180042380 -> "TextDescription"
//
// `??_7LabelDescription@UI@@6B@` @ 0x180156b30, TEN slots:
//   [0] 0x180042730 vector deleting dtor · [1] 0x1800423b0 TypeName
//   [2] 0x180042390 Type · [3] 0x1800d2950 GetFileVersion (0x2304)
//   [4] External::Compile, inherited · [5] 0x1800d2820 Serialize
//   [6] 0x180042530 Reconstruct · [7] 0x180042560 Destruct
//   [8] 0x1800d26b0 CreateInstance RETSLOT (B6-2) · [9] 0x180042550 Construct
//   — OVERRIDES TextDescription's slot 9 (`override`, not `virtual`).
//
// SIZE 0xb8 (`MOV EDX,0xb8` in 0x180042730). LAYOUT, from the ctor
// `??0LabelDescription@UI@@QEAA@XZ` (0x1800d29d0, read as DISASSEMBLY by block
// C3: `CALL ??0TextDescription` then):
//     +0x90 dword 3     _textAlignment  kScalarUInt  -> LabelElement +0x110 (_align)
//     +0x94 QWORD 1     _labelPosition  kScalarUInt  -> +0x114 (_mode)     } one store
//                       _textWidth      kScalarInt   -> +0x118 (_minTextWidth)
//     +0x9c dword 2     _spacing        kScalarInt   -> +0x11c (_gap)
//     +0xa0 QWORD -1    _imageWidth     kScalarInt   -> +0x120 (_iconWidth) } one store
//                       _imageHeight    kScalarInt   -> +0x124 (_iconHeight)
//     +0xa8 qword 0     _spriteSheet  ExternalPtr<UI::SpriteSheet> (0x180004590)
//     +0xb0 qword 0     _spriteName   System::String (0x1800c9930)
// The field names are the POD's (DescriptionsBatch1.h); where each one lands on
// the element is CreateInstance's copy (labeldescription.cpp), recorded above.
//
// ENDGAME B.3 step 9 (block UI-A, B39_RULES D1/D2): CONCRETE. The real public
// ctor is retail's `??0LabelDescription@UI@@QEAA@XZ` (0x1800d29d0) minus its vptr stores;
// slot 0 is the class's own vector deleting dtor; ~LabelDescription is public and
// virtual. The factory pair is the class's own; the minted
// `??_7LabelDescription@UI@@6B@`, its RT_DELDTOR row, the factory-interpreter row and
// the doors are retired.
// ============================================================================

#include <type_traits>
#include <cstdint>
#include <cstddef>

#include "TextDescription.h"
#include "DescriptionsBatch1.h"   // kVtbl* names; RtUI::LabelDescription aliases this class
#include "../object/Internal.h"   // Object::detail::InternalMemFree

namespace UI {

class SpriteSheet;

class LabelDescription : public TextDescription {
public:
    // ??0LabelDescription@UI@@QEAA@XZ 0x1800d29d0 — the REAL constructor: CALL
    // ??0TextDescription (TextDescription()), then +0x90 DWORD 3, ONE QWORD 1
    // over +0x94/+0x98, +0x9c DWORD 2, ONE QWORD -1 over +0xa0/+0xa4, +0xa8
    // and +0xb0 QWORD zero.
    LabelDescription() : TextDescription() {
        _textAlignment = MidLeft;
        _labelPosition = 1;
        _textWidth = 0;
        _spacing = 2;
        _imageWidth = -1;
        _imageHeight = -1;
        _spriteSheet = nullptr;
        // +0xb0 `_spriteName`: its own String() (the zero store) runs as a member.
    }

    // [0] 0x180042730 — MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0xb8`, the element size).
    // `??1LabelDescription@UI@@UEAA@XZ` (0x1800d2960). VIRTUAL (B3X W1)
    // (labeldescription.cpp): releases the sprite name at +0xb0; the compiler
    // chains ~TextDescription. +0xa8 is NOT released.
    ~LabelDescription() override;
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x1800423b0, Type 0x180042390, TypeId 0x1800423a0, _TypeName 0x180042370
    //         _ParentTypeName 0x180042380, CreateNewObject 0x1800424a0, CreateInPlaceObject 0x180042520
    DECLARE_TYPE(LabelDescription)
public:
    int32_t     GetFileVersion() const override;                 // [3] 0x1800d2950
    bool        Serialize(IO::Stream& stream) override;                // [5] 0x1800d2820
    Object::External* Reconstruct() override;                    // [6] 0x180042530
    void        Destruct() override;                             // [7] 0x180042560
    Object::InternalPtr<Element>* CreateInstance(
        Object::InternalPtr<Element>* ret,
        const Dialog* dialog) const override;                    // [8] 0x1800d26b0 RETSLOT (B6-2)
    Object::External* Construct() override;                      // [9] 0x180042550

    LabelDescription(const LabelDescription& src);               // 0x180042570  not ported
    LabelDescription& operator=(const LabelDescription& src);    // 0x180042630  not ported
    static void* operator new(std::size_t bytes);                // 0x1800423c0  not ported
    static void* operator new(std::size_t bytes, void* place);   // 0x180042430  not ported
    static void  operator delete(void* p) { ::Object::detail::InternalMemFree(p); }                       // 0x180042440  not ported

private:

public:
    uint32_t     _textAlignment;        // +0x90  kScalarUInt, ctor 3
    uint32_t     _labelPosition;        // +0x94  kScalarUInt, ctor 1
    int32_t      _textWidth;        // +0x98  kScalarInt,  ctor 0
    int32_t      _spacing;        // +0x9c  kScalarInt,  ctor 2
    int32_t      _imageWidth;        // +0xa0  kScalarInt,  ctor -1
    int32_t      _imageHeight;        // +0xa4  kScalarInt,  ctor -1
    SpriteSheet* _spriteSheet;   // +0xa8  ExternalPtr<UI::SpriteSheet>
    System::String _spriteName;  // +0xb0  ctor 0x1800d29d0 zero store, dtor 0x1800d2960 release
};

static_assert(sizeof(LabelDescription) == 0xb8,
              "UI::LabelDescription is 0xb8 (MOV EDX,0xb8 in 0x180042730)");
static_assert(alignof(LabelDescription) == 8, "UI::LabelDescription aligns to 8");
static_assert(std::has_virtual_destructor<LabelDescription>::value, "B3X W1: the normal-virtual cutover");
static_assert(!std::is_abstract<LabelDescription>::value, "B39 D1: concrete");
static_assert(offsetof(LabelDescription, _textAlignment) == sizeof(TextDescription),
              "LabelDescription's own fields begin at the end of TextDescription");
static_assert(offsetof(LabelDescription, _textAlignment)      == 0x90, "");
static_assert(offsetof(LabelDescription, _labelPosition)      == 0x94, "");
static_assert(offsetof(LabelDescription, _textWidth)      == 0x98, "");
static_assert(offsetof(LabelDescription, _spacing)      == 0x9c, "");
static_assert(offsetof(LabelDescription, _imageWidth)      == 0xa0, "");
static_assert(offsetof(LabelDescription, _imageHeight)      == 0xa4, "");
static_assert(offsetof(LabelDescription, _spriteSheet) == 0xa8, "");
static_assert(offsetof(LabelDescription, _spriteName)  == 0xb0, "");
// RtUI::LabelDescription is an ALIAS of this class since fragment pass F2; the
// POD's offsets are the absolute asserts above.

}  // namespace UI
