#pragma once
// ============================================================================
// src/engine/ui/NumberDescription.h — UI::NumberDescription, the REAL class
// ============================================================================
//
// ENDGAME B.3 step 6, block C3. No header existed; the layout lived as raw
// offsets in src/engine/ui/numberdescription.cpp (RULE B6-8), MOVED here.
//
// PARENT `UI::ElementDescription` — READ (B36_SLOTS.md section 3), RECORDED:
//     ?_TypeName@NumberDescription@UI@@CAPEBDXZ       @0x18004a200 -> "NumberDescription"
//     ?_ParentTypeName@NumberDescription@UI@@CAPEBDXZ @0x18004a210 -> "ElementDescription"
//
// `??_7NumberDescription@UI@@6B@` @ 0x180157530, TEN slots: [0] 0x18004a760 ·
// [1] 0x18004a240 TypeName · [2] 0x18004a220 Type · [3] 0x180118e20
// GetFileVersion (0x104) · [4] External::Compile, inherited · [5] 0x180118e30
// Serialize · [6] 0x18004a4c0 Reconstruct · [7] 0x18004a610 Destruct ·
// [8] 0x180118f10 CreateInstance RETSLOT (B6-2) · [9] 0x18004a570 Construct.
//
// SIZE 0x88 (`MOV EDX,0x88` at 0x18004a77f). The ctor is INLINED into
// Construct and Reconstruct (no ??0 export is called by either; the exported
// `??0NumberDescription@UI@@QEAA@XZ` 0x180118b60 is ui_factory_wave28.cpp's):
//     +0x68 qword 0   _font  ExternalPtr<UI::Font> (0x18003f1e0) -> NumberElement::_font
//     +0x70 qword 0   _numberType kScalarUInt  -> NumberElement::_mode      } one
//                     _ivalue     kScalarInt   -> NumberElement::_intValue  } store
//     +0x78 dword 0   _fvalue     kScalarFloat -> NumberElement::_floatValue
//     +0x7c byte  0   _showSign   kScalarBool  -> NumberElement::_showSign
//     +0x7d..+0x87 never written
// The `NumberType` enum `?GetNumberType@NumberElement@UI@@` returns is nested
// here (`W4NumberType@NumberDescription@2@`); its enumerator names are not in
// the binary, and none is invented (0 = int, 1 = float per CacheText).
//
// ENDGAME B.3 step 9 (block UI-A, B39_RULES D1/D2): CONCRETE. The real public
// ctor is retail's `??0NumberDescription@UI@@QEAA@XZ` (0x180118b60) minus its vptr stores;
// slot 0 is the class's own vector deleting dtor; ~NumberDescription is public and
// virtual. The factory pair is the class's own; the minted
// `??_7NumberDescription@UI@@6B@`, its RT_DELDTOR row, the factory-interpreter row and
// the doors are retired.
// ============================================================================

#include <type_traits>
#include <cstdint>
#include <cstddef>

#include "ElementDescription.h"
#include "../object/Internal.h"   // Object::detail::InternalMemFree

namespace UI {

class Dialog;
class Element;
class Font;

class NumberDescription : public ElementDescription {
public:
    // A `W4` enum: four bytes, returned in EAX.
    enum NumberType : int32_t {};

    // ??0NumberDescription@UI@@QEAA@XZ 0x180118b60 — the REAL constructor, the
    // same stores Construct / Reconstruct inline (0x18004a586..0x18004a600):
    // ElementDescription()'s stores, then +0x68 QWORD 0, +0x70 QWORD 0 (both
    // _numberType and _ivalue), +0x78 DWORD 0 (0.0f), +0x7c BYTE 0. The tail pad stays
    // unwritten.
    NumberDescription() : ElementDescription() {
        _font = nullptr;
        _numberType  = 0;
        _ivalue  = 0;
        _fvalue  = 0.0f;
        _showSign  = 0;
    }

    // [0] 0x18004a760 — MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0x88`, the element size).
    // `??1NumberDescription@UI@@UEAA@XZ` (0x180118c00). EMPTY
    // (numberdescription.cpp): only the base's +0x60 is released; the
    // compiler chains ~ElementDescription and ~External.
    ~NumberDescription() override;
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x18004a240, Type 0x18004a220, TypeId 0x18004a230, _TypeName 0x18004a200
    //         _ParentTypeName 0x18004a210, CreateNewObject 0x18004a330, CreateInPlaceObject 0x18004a420
    DECLARE_TYPE(NumberDescription)
public:
    int32_t     GetFileVersion() const override;                 // [3] 0x180118e20
    bool        Serialize(IO::Stream& stream) override;                // [5] 0x180118e30
    Object::External* Reconstruct() override;                    // [6] 0x18004a4c0
    void        Destruct() override;                             // [7] 0x18004a610
    Object::InternalPtr<Element>* CreateInstance(
        Object::InternalPtr<Element>* ret,
        const Dialog* dialog) const override;                    // [8] 0x180118f10 RETSLOT (B6-2)
    virtual Object::External* Construct();                       // [9] 0x18004a570 — INTRODUCED

    NumberDescription(const NumberDescription& src);             // 0x18004a620  not ported
    NumberDescription& operator=(const NumberDescription& src);  // 0x18004a690  not ported
    static void* operator new(std::size_t bytes);                // 0x18004a250  not ported
    static void* operator new(std::size_t bytes, void* place);   // 0x18004a2c0  not ported
    static void  operator delete(void* p) { ::Object::detail::InternalMemFree(p); }                       // 0x18004a2d0  not ported

private:

public:
    Font*    _font;       // +0x68  ExternalPtr<UI::Font>
    uint32_t _numberType;        // +0x70  kScalarUInt
    int32_t  _ivalue;        // +0x74  kScalarInt
    float    _fvalue;        // +0x78  kScalarFloat (the ctor stores integer 0 = 0.0f)
    bool     _showSign;        // +0x7c  kScalarBool
    uint8_t  _pad7d[11];  // +0x7d  never written
};

static_assert(sizeof(NumberDescription) == 0x88,
              "UI::NumberDescription is 0x88 (MOV EDX,0x88 at 0x18004a77f)");
static_assert(offsetof(NumberDescription, _pad7d) == 0x7D, "NumberDescription::_pad7d");
static_assert(alignof(NumberDescription) == 8, "UI::NumberDescription aligns to 8");
static_assert(std::has_virtual_destructor<NumberDescription>::value, "B3X W1: the normal-virtual cutover");
static_assert(!std::is_abstract<NumberDescription>::value, "B39 D1: concrete");
static_assert(sizeof(NumberDescription::NumberType) == 4, "a W4 enum is four bytes");
static_assert(offsetof(NumberDescription, _font) == sizeof(ElementDescription), "");
static_assert(offsetof(NumberDescription, _font) == 0x68, "");
static_assert(offsetof(NumberDescription, _numberType)  == 0x70, "");
static_assert(offsetof(NumberDescription, _ivalue)  == 0x74, "");
static_assert(offsetof(NumberDescription, _fvalue)  == 0x78, "");
static_assert(offsetof(NumberDescription, _showSign)  == 0x7c, "");

}  // namespace UI
