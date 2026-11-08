#pragma once
// ============================================================================
// src/engine/ui/EditDescription.h — UI::EditDescription, the REAL C++ class
// ============================================================================
//
// ENDGAME B.3 step 6, block C3. The POD `RtUI::EditDescription` stays in
// DescriptionsBatch1.h untouched (RULE B6-4.4); this class is cross-asserted
// against it at the bottom.
//
// PARENT `UI::ElementDescription` — READ (B36_SLOTS.md section 3), RECORDED:
//     ?_TypeName@EditDescription@UI@@CAPEBDXZ       @0x18003f690 -> "EditDescription"
//     ?_ParentTypeName@EditDescription@UI@@CAPEBDXZ @0x18003f6a0 -> "ElementDescription"
//
// `??_7EditDescription@UI@@6B@` @ 0x180156880, TEN slots: [0] 0x18003fa20 ·
// [1] 0x18003f6d0 TypeName · [2] 0x18003f6b0 Type · [3] 0x1800d11e0
// GetFileVersion (0x204) · [4] External::Compile, inherited · [5] 0x1800d1110
// Serialize · [6] 0x18003f850 Reconstruct · [7] 0x18003f880 Destruct ·
// [8] 0x1800d0f90 CreateInstance RETSLOT (B6-2) · [9] 0x18003f870 Construct
// (INTRODUCED).
//
// SIZE 0x88 (`MOV EDX,0x88` in 0x18003fa20). LAYOUT, the ctor
// `??0EditDescription@UI@@QEAA@XZ` (0x1800d12c0, read as DISASSEMBLY by block
// C3) after ElementDescription's +0x60:
//     +0x68 qword 0   _font                ExternalPtr<UI::Font> (0x18003f1e0) -> EditElement::_font
//     +0x70 qword 0   _initialText         System::String                      -> EditElement::_text
//     +0x78 dword 0   _maxLength           kScalarInt                          -> EditElement::_maxLength
//     +0x7c byte  0   _password            kScalarBool                         -> EditElement::_password
//     +0x7d..+0x7f    NOT written
//     +0x80 qword 0   _allowableCharacters System::String                      -> EditElement::_filter
// Names: the POD's. Where each lands is CreateInstance's copy (editdescription.cpp).
//
// ENDGAME B.3 step 9 (block UI-A, B39_RULES D1/D2): CONCRETE. The real public
// ctor is retail's `??0EditDescription@UI@@QEAA@XZ` (0x1800d12c0) minus its vptr stores;
// slot 0 is the class's own vector deleting dtor; ~EditDescription is public and
// virtual. The factory pair is the class's own; the minted
// `??_7EditDescription@UI@@6B@`, its RT_DELDTOR row, the factory-interpreter row and
// the doors are retired.
// ============================================================================

#include <type_traits>
#include <cstdint>
#include <cstddef>

#include "ElementDescription.h"
#include "DescriptionsBatch1.h"   // kVtbl* names; RtUI::EditDescription aliases this class
#include "../object/Internal.h"   // Object::detail::InternalMemFree

namespace UI {

class Dialog;
class Element;
class Font;

class EditDescription : public ElementDescription {
public:
    // ??0EditDescription@UI@@QEAA@XZ 0x1800d12c0 — the REAL constructor:
    // ElementDescription()'s stores, then (0x1800d133d..0x1800d134b) +0x68 and
    // +0x70 QWORD 0, +0x78 DWORD 0, +0x7c BYTE 0, +0x80 QWORD 0. The three pad
    // bytes after +0x7c stay unwritten.
    EditDescription() : ElementDescription() {
        _font    = nullptr;
        // +0x70 / +0x80: the System::String members' own String() (B4 Y RT-UI).
        _maxLength   = 0;
        _password   = 0;
    }

    // [0] 0x18003fa20 — MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0x88`, the element size).
    // `??1EditDescription@UI@@UEAA@XZ` (0x1800d11f0). VIRTUAL (B3X W1);
    // editdescription.cpp holds retail's body (+0x80 then +0x70 released); the
    // compiler chains ~ElementDescription (+0x60) and ~External.
    ~EditDescription() override;
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x18003f6d0, Type 0x18003f6b0, TypeId 0x18003f6c0, _TypeName 0x18003f690
    //         _ParentTypeName 0x18003f6a0, CreateNewObject 0x18003f7c0, CreateInPlaceObject 0x18003f840
    DECLARE_TYPE(EditDescription)
public:
    int32_t     GetFileVersion() const override;                 // [3] 0x1800d11e0
    bool        Serialize(IO::Stream& stream) override;                // [5] 0x1800d1110
    Object::External* Reconstruct() override;                    // [6] 0x18003f850
    void        Destruct() override;                             // [7] 0x18003f880
    Object::InternalPtr<Element>* CreateInstance(
        Object::InternalPtr<Element>* ret,
        const Dialog* dialog) const override;                    // [8] 0x1800d0f90 RETSLOT (B6-2)
    virtual Object::External* Construct();                       // [9] 0x18003f870 — INTRODUCED

    EditDescription(const EditDescription& src);                 // 0x18003f890  not ported
    EditDescription& operator=(const EditDescription& src);      // 0x18003f920  not ported
    static void* operator new(std::size_t bytes);                // 0x18003f6e0  not ported
    static void* operator new(std::size_t bytes, void* place);   // 0x18003f750  not ported
    static void  operator delete(void* p) { ::Object::detail::InternalMemFree(p); }                       // 0x18003f760  not ported

private:

public:
    Font*   _font;       // +0x68  ExternalPtr<UI::Font>
    System::String _initialText;  // +0x70  ctor 0x1800d12c0, dtor 0x1800d11f0 (second release)
    int32_t _maxLength;      // +0x78  kScalarInt
    bool    _password;      // +0x7c  kScalarBool (B4: retail's SerializeBool operand)
    uint8_t _pad7d[3];    // +0x7d  never written
    System::String _allowableCharacters;  // +0x80  ctor 0x1800d12c0, dtor 0x1800d11f0 (first release)
};

static_assert(sizeof(EditDescription) == 0x88,
              "UI::EditDescription is 0x88 (MOV EDX,0x88 in 0x18003fa20)");
static_assert(offsetof(EditDescription, _pad7d) == 0x7D, "EditDescription::_pad7d");
static_assert(alignof(EditDescription) == 8, "UI::EditDescription aligns to 8");
static_assert(std::has_virtual_destructor<EditDescription>::value, "B3X W1: the normal-virtual cutover");
static_assert(!std::is_abstract<EditDescription>::value, "B39 D1: concrete");
static_assert(offsetof(EditDescription, _font) == sizeof(ElementDescription), "");
static_assert(offsetof(EditDescription, _font)    == 0x68, "");
static_assert(offsetof(EditDescription, _initialText) == 0x70, "");
static_assert(offsetof(EditDescription, _maxLength)   == 0x78, "");
static_assert(offsetof(EditDescription, _password)   == 0x7c, "");
static_assert(offsetof(EditDescription, _allowableCharacters) == 0x80, "");
// RtUI::EditDescription is an ALIAS of this class since fragment pass F2; the
// POD's offsets are the absolute asserts above.

}  // namespace UI
