#pragma once
// ============================================================================
// src/engine/ui/NumberEditDescription.h — UI::NumberEditDescription
// ============================================================================
//
// ENDGAME B.3 step 6, block C3. No header existed; the layout lived as raw
// offsets in src/engine/ui/numbereditdescription.cpp (RULE B6-8), MOVED here.
//
// PARENT `UI::ElementDescription` — NOT EditDescription. READ (B36_SLOTS.md
// section 3), RECORDED, NOT USED:
//     ?_TypeName@NumberEditDescription@UI@@CAPEBDXZ       @0x180045e50 -> "NumberEditDescription"
//     ?_ParentTypeName@NumberEditDescription@UI@@CAPEBDXZ @0x180045e60 -> "ElementDescription"
// (Its ELEMENT derives from EditElement; the description does not derive from
// EditDescription — the two hierarchies differ here, and the literal decides.)
//
// `??_7NumberEditDescription@UI@@6B@` @ 0x180156fe0, TEN slots: [0] 0x180046190
// · [1] 0x180045e90 TypeName · [2] 0x180045e70 Type · [3] 0x1800d4fd0
// GetFileVersion (0x1004) · [4] External::Compile, inherited · [5] 0x1800d4ee0
// Serialize · [6] 0x180046010 Reconstruct · [7] 0x180046040 Destruct ·
// [8] 0x1800d4cc0 CreateInstance RETSLOT (B6-2) · [9] 0x180046030 Construct.
//
// SIZE 0x88 (`MOV EDX,0x88` at 0x1800461af). LAYOUT, from the ctor
// `??0NumberEditDescription@UI@@QEAA@XZ` (0x1800d5050, transcribed in
// numbereditdescription.cpp) after ElementDescription's 0x68:
//     +0x68 qword 0      _font  ExternalPtr<UI::Font> (0x18003f1e0)
//     +0x70              _initialValue  *** NEVER WRITTEN BY THE CONSTRUCTOR *** —
//                                       serialized (kScalarInt) and formatted into the
//                                       element's initial text with "%i"
//     +0x74 dword 6      _maxLength     -> EditElement::_maxLength
//     +0x78 dword -32767 _minValue      -> NumberEditElement::_min
//     +0x7c dword 0x7fff _maxValue      -> NumberEditElement::_max
//     +0x80 byte  1      _allowNegative "allow negative": selects "0123456789-"
//     +0x81..+0x87 never written
// Names: the ported file's; meanings where CreateInstance proves them.
//
// ENDGAME B.3 step 9 (block UI-A, B39_RULES D1/D2): CONCRETE. The real public
// ctor is retail's `??0NumberEditDescription@UI@@QEAA@XZ` (0x1800d5050) minus its vptr stores;
// slot 0 is the class's own vector deleting dtor; ~NumberEditDescription is public and
// virtual. The factory pair is the class's own; the minted
// `??_7NumberEditDescription@UI@@6B@`, its RT_DELDTOR row, the factory-interpreter row and
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

class NumberEditDescription : public ElementDescription {
public:
    // ??0NumberEditDescription@UI@@QEAA@XZ 0x1800d5050 — the REAL constructor:
    // ElementDescription()'s stores, then +0x68 QWORD 0, (+0x70 NOT written —
    // see numbereditdescription.cpp's banner), +0x74 6, +0x78 0xffff8001,
    // +0x7c 0x7fff, +0x80 BYTE 1 (0x1800d50cd..0x1800d50e6).
    NumberEditDescription() : ElementDescription() {
        _font = nullptr;
        _maxLength  = 6;
        _minValue  = static_cast<int32_t>(0xffff8001u);
        _maxValue  = 0x7fff;
        _allowNegative  = 1;
    }

    // [0] 0x180046190 — MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0x88`, the element size).
    // `??1NumberEditDescription@UI@@UEAA@XZ` (0x1800d4fe0). EMPTY (numbereditdescription.cpp): only the base's +0x60 is released
    // (the compiler chains ~ElementDescription and ~External).
    ~NumberEditDescription() override;
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x180045e90, Type 0x180045e70, TypeId 0x180045e80, _TypeName 0x180045e50
    //         _ParentTypeName 0x180045e60, CreateNewObject 0x180045f80, CreateInPlaceObject 0x180046000
    DECLARE_TYPE(NumberEditDescription)
public:
    int32_t     GetFileVersion() const override;                 // [3] 0x1800d4fd0
    bool        Serialize(IO::Stream& stream) override;                // [5] 0x1800d4ee0
    Object::External* Reconstruct() override;                    // [6] 0x180046010
    void        Destruct() override;                             // [7] 0x180046040
    Object::InternalPtr<Element>* CreateInstance(
        Object::InternalPtr<Element>* ret,
        const Dialog* dialog) const override;                    // [8] 0x1800d4cc0 RETSLOT (B6-2)
    virtual Object::External* Construct();                       // [9] 0x180046030 — INTRODUCED

    NumberEditDescription(const NumberEditDescription& src);             // 0x180046050  not ported
    NumberEditDescription& operator=(const NumberEditDescription& src);  // 0x1800460c0  not ported
    static void* operator new(std::size_t bytes);                // 0x180045ea0  not ported
    static void* operator new(std::size_t bytes, void* place);   // 0x180045f10  not ported
    static void  operator delete(void* p) { ::Object::detail::InternalMemFree(p); }                       // 0x180045f20  not ported

private:

public:
    Font*   _font;       // +0x68  ExternalPtr<UI::Font>
    int32_t _initialValue;        // +0x70  NEVER written by the ctor
    int32_t _maxLength;        // +0x74  ctor 6
    int32_t _minValue;        // +0x78  ctor 0xffff8001
    int32_t _maxValue;        // +0x7c  ctor 0x7fff
    bool    _allowNegative;        // +0x80  ctor 1 (B4: kScalarBool, retail's type)
    uint8_t _pad81[7];   // +0x81  never written
};

static_assert(sizeof(NumberEditDescription) == 0x88,
              "UI::NumberEditDescription is 0x88 (MOV EDX,0x88 at 0x1800461af)");
static_assert(offsetof(NumberEditDescription, _pad81) == 0x81, "NumberEditDescription::_pad81");
static_assert(alignof(NumberEditDescription) == 8, "UI::NumberEditDescription aligns to 8");
static_assert(std::has_virtual_destructor<NumberEditDescription>::value, "B3X W1: the normal-virtual cutover");
static_assert(!std::is_abstract<NumberEditDescription>::value, "B39 D1: concrete");
static_assert(offsetof(NumberEditDescription, _font) == sizeof(ElementDescription), "");
static_assert(offsetof(NumberEditDescription, _font) == 0x68, "");
static_assert(offsetof(NumberEditDescription, _initialValue)  == 0x70, "");
static_assert(offsetof(NumberEditDescription, _maxLength)  == 0x74, "");
static_assert(offsetof(NumberEditDescription, _minValue)  == 0x78, "");
static_assert(offsetof(NumberEditDescription, _maxValue)  == 0x7c, "");
static_assert(offsetof(NumberEditDescription, _allowNegative)  == 0x80, "");

}  // namespace UI
