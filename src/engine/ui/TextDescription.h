#pragma once
// ============================================================================
// src/engine/ui/TextDescription.h — UI::TextDescription, the REAL C++ class
// ============================================================================
//
// ENDGAME B.3 step 6, block C3. Before this block the class had no header: its
// layout lived as raw `p + 0x..` offsets in src/engine/ui/textdescription.cpp
// (RULE B6-8). Those offsets are MOVED here under the retail name; the bodies
// now read the named fields.
//
// PARENT `UI::ElementDescription` — READ, not inferred (B36_SLOTS.md section 3):
//     ?_TypeName@TextDescription@UI@@CAPEBDXZ       @0x180041a90 -> "TextDescription"
//     ?_ParentTypeName@TextDescription@UI@@CAPEBDXZ @0x180041aa0 -> "ElementDescription"
// RECORDED, NOT USED (B5-15 / B6-7): no DEFINE_TYPE in step 6; slots 1/2 read
// the minted `?_typeInfo@TextDescription@UI@@0VBaseTypeInfo@Object@@A`
// (0x1801faeb0).
//
// `??_7TextDescription@UI@@6B@` @ 0x180156be0, TEN slots (B36_SLOTS.md sec. 1):
//   [0] +0x00 0x180041e30 vector deleting dtor  (deleting_dtor.cpp row)
//   [1] +0x08 0x180041ad0 TypeName
//   [2] +0x10 0x180041ab0 Type
//   [3] +0x18 0x18011ee20 GetFileVersion   (0x304)
//   [4] +0x20 0x1800066e0 Object::External::Compile — inherited, NOT declared
//   [5] +0x28 0x18011ee30 Serialize
//   [6] +0x30 0x180041c50 Reconstruct
//   [7] +0x38 0x180041c80 Destruct
//   [8] +0x40 0x18011f3d0 CreateInstance(const Dialog*) -> InternalPtr<Element>
//                          RETSLOT (RULE B6-2)
//   [9] +0x48 0x180041c70 Construct — INTRODUCED here (RULE B6-1.1)
//
// SIZE 0x90: `MOV EDX,0x90` in the slot-0 vector deleting destructor
// (0x180041e4f). LAYOUT: the ctor `??0TextDescription@UI@@QEAA@XZ`
// (0x18011ea20) writes, after ElementDescription's 0x68:
//     +0x68 qword 0  ExternalPtr<UI::Font>         Serialize: 0x18003f1e0
//     +0x70 qword 0  ExternalPtr<UI::StringTable>  Serialize: 0x1800042a0
//     +0x78 qword 0  System::String (the string NAME)  0x1800c9930
//     +0x80 dword 0  kScalarUInt
//     +0x84 WORD  0  two kScalarBool bytes (+0x84, +0x85) in ONE store
//     +0x86 ..+0x87  NEVER WRITTEN by the ctor and read by no body
//     +0x88 dword 0xc8  kScalarInt
//     +0x8c dword -1    kScalarInt, serialized CONDITIONALLY (textdescription.cpp)
// The scalar names are the ported file's (`_u80`/`_b84`/`_b85`/`_maxWidth`; the +0x88 dword is `_wordWrapSize`):
// Serialize proves the widths, nothing proves a meaning. +0x88 is the width a wrapped text breaks at (the kit's
// Mod.rsc writes `int _wordWrapSize = 192`), so it carries that name here and in TextElement.
//
// (Until ENDGAME B.3.9:) ABSTRACT (slot 0 `= 0`); no real ctor (B4b-2): every live one is built by the
// `??0TextDescription` DOOR (textdescription.cpp) and wears the MINTED table,
// so every door delegates with a QUALIFIED call.
//
// ENDGAME B.3 step 9 (block UI-A, B39_RULES D1/D2): CONCRETE. The real public
// ctor is retail's `??0TextDescription@UI@@QEAA@XZ` (0x18011ea20) minus its vptr stores;
// slot 0 is the class's own vector deleting dtor; ~TextDescription is public and
// virtual. The factory pair is the class's own; the minted
// `??_7TextDescription@UI@@6B@`, its RT_DELDTOR row, the factory-interpreter row and
// the doors are retired.
// ============================================================================

#include <type_traits>
#include <cstdint>
#include <cstddef>

#include "ElementDescription.h"
#include "../object/Internal.h"

namespace UI {

class Dialog;
class Element;
class Font;
class StringTable;
class TextElement;

class TextDescription : public ElementDescription {
public:
    // ??0TextDescription@UI@@QEAA@XZ 0x18011ea20 — the REAL constructor
    // (out of line in retail; Construct / Reconstruct tail-jump to it):
    // ElementDescription()'s stores, then +0x68/+0x70/+0x78 QWORD zero, +0x80
    // DWORD 0, ONE WORD 0 over +0x84/+0x85, +0x88 DWORD 0xc8, +0x8c DWORD -1.
    // +0x86..+0x87 are NOT written.
    TextDescription() : ElementDescription() {
        _font         = nullptr;
        _stringTable2 = nullptr;
        // +0x78 `_name`: its own String() (the zero store) runs as a member.
        _justify = 0;
        _dropShadow = false;
        _wordWrap = false;
        _wordWrapSize = 0xc8;
        _maxWidth = -1;
    }

    // ---- the ten slots, in measured order ---------------------------------
    // [0] 0x180041e30 — MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0x90`, the element size).
    // `??1TextDescription@UI@@UEAA@XZ` (0x18011eae0). VIRTUAL (B3X W1)
    // (textdescription.cpp): releases the name String at +0x78; the compiler
    // chains ~ElementDescription (+0x60) and ~External. +0x68 / +0x70 are NOT
    // released (retail's).
    ~TextDescription() override;
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x180041ad0, Type 0x180041ab0, TypeId 0x180041ac0, _TypeName 0x180041a90
    //         _ParentTypeName 0x180041aa0, CreateNewObject 0x180041bc0, CreateInPlaceObject 0x180041c40
    DECLARE_TYPE(TextDescription)
public:
    int32_t     GetFileVersion() const override;                 // [3] 0x18011ee20
    // [4] Compile — Object::External's own body, inherited (B6-1.5).
    bool        Serialize(IO::Stream& stream) override;                // [5] 0x18011ee30 (IO::Stream&)
    Object::External* Reconstruct() override;                    // [6] 0x180041c50
    void        Destruct() override;                             // [7] 0x180041c80
    Object::InternalPtr<Element>* CreateInstance(
        Object::InternalPtr<Element>* ret,
        const Dialog* dialog) const override;                    // [8] 0x18011f3d0 RETSLOT (B6-2)
    virtual Object::External* Construct();                       // [9] 0x180041c70 — INTRODUCED

    // ---- non-virtual members ------------------------------------------------
    // ?CreateInstance@TextDescription@UI@@QEBAXV?$InternalPtr@VTextElement@UI@@@Object@@PEBVDialog@2@@Z
    //   0x18011ef70 — the "stamp this description onto that element" applier.
    //   `V?$InternalPtr` BY VALUE, which retail passes by HIDDEN POINTER
    //   (RULE B6-11), so the parameter is spelled as the pointer. Its body is
    //   the registered door `RtUi_TextDescription_CreateInstance` in
    //   element_bodies_b5.cpp (NOT this block's file — fragment pass); declared
    //   here, left undefined. Slot 8 and LabelDescription reach it by address.
    void CreateInstance(Object::InternalPtr<TextElement>* element,
                        const Dialog* dialog) const;

    TextDescription(const TextDescription& src);                 // 0x180041c90  not ported
    TextDescription& operator=(const TextDescription& src);      // 0x180041d40  not ported

    // Reflection statics / allocation. `_typeInfo` is the MINTED record.
    static void* operator new(std::size_t bytes);                // 0x180041ae0  not ported
    static void* operator new(std::size_t bytes, void* place);   // 0x180041b50  not ported
    static void  operator delete(void* p) { ::Object::detail::InternalMemFree(p); }                       // 0x180041b60  not ported

private:

public:
    // ---- the 0x28 bytes TextDescription adds (public: doors and the applier
    // in element_bodies_b5.cpp read them) ------------------------------------
    Font*        _font;          // +0x68  ExternalPtr<UI::Font>
    StringTable* _stringTable2;  // +0x70  ExternalPtr<UI::StringTable> (the base's +0x58 is another)
    System::String _name;        // +0x78  ctor 0x18011ea20 zero store, dtor 0x18011eae0 release
    uint32_t     _justify;           // +0x80  kScalarUInt
    bool         _dropShadow;           // +0x84  kScalarBool
    bool         _wordWrap;           // +0x85  kScalarBool
    uint8_t      _pad86[2];      // +0x86  never written
    int32_t      _wordWrapSize;  // +0x88  kScalarInt, ctor 0xc8
    int32_t      _maxWidth;           // +0x8c  kScalarInt, ctor -1, conditional
};

static_assert(sizeof(TextDescription) == 0x90,
              "UI::TextDescription is 0x90 (MOV EDX,0x90 in 0x180041e30)");
static_assert(offsetof(TextDescription, _pad86) == 0x86, "TextDescription::_pad86");
static_assert(alignof(TextDescription) == 8, "UI::TextDescription aligns to 8");
static_assert(std::has_virtual_destructor<TextDescription>::value, "B3X W1: the normal-virtual cutover");
static_assert(!std::is_abstract<TextDescription>::value, "B39 D1: concrete");
static_assert(offsetof(TextDescription, _font) == sizeof(ElementDescription),
              "TextDescription's own fields begin at the end of ElementDescription");
static_assert(offsetof(TextDescription, _font)         == 0x68, "");
static_assert(offsetof(TextDescription, _stringTable2) == 0x70, "");
static_assert(offsetof(TextDescription, _name)         == 0x78, "");
static_assert(offsetof(TextDescription, _justify)          == 0x80, "");
static_assert(offsetof(TextDescription, _dropShadow)          == 0x84, "");
static_assert(offsetof(TextDescription, _wordWrap)          == 0x85, "");
static_assert(offsetof(TextDescription, _wordWrapSize)   == 0x88, "");
static_assert(offsetof(TextDescription, _maxWidth)          == 0x8c, "");

}  // namespace UI
